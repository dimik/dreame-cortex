#!/usr/bin/env python3
"""valetudo_bridge.py — Valetudo REST/SSE -> ROS 2 bridge.

Runs in the robot's chroot (ROS 2 Jazzy). Publishes the robot's map + pose to ROS WITHOUT an MQTT
broker — it just polls Valetudo's HTTP API (localhost). The raw IMU/odometry over /dev/ttyS4 is
deferred (the LD_PRELOAD read-tap destabilises AVA — see docs/sensors.md / mcutap.c); this covers
the v1 nav needs from what Valetudo already exposes.

Publishes:
  /map        nav_msgs/OccupancyGrid   <- Valetudo floor(0)/wall(100) layers, unknown=-1
  /odom       nav_msgs/Odometry        <- robot_position entity (pose only; no twist)
  TF map->base_link                    <- same pose
  /robot/status  std_msgs/String       <- Valetudo StatusStateAttribute (from the map poll)

Coordinate convention: Valetudo is mm, +y DOWN (image coords). ROS (REP-103) is m, +y UP. So
  x_ros = x_mm/1000 ;  y_ros = -y_mm/1000 ;  yaw_ros = -angle_deg.
Map pixels are pixel indices (×pixelSize mm). The OccupancyGrid origin is the bottom-left cell.

Run:  source /opt/ros/jazzy/setup.bash && python3 valetudo_bridge.py [--host http://127.0.0.1] [--rate 1.0]
"""
import argparse, json, math, urllib.request
from array import array

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSDurabilityPolicy, QoSReliabilityPolicy
from nav_msgs.msg import OccupancyGrid, Odometry
from std_msgs.msg import String
from tf2_ros import TransformBroadcaster
from geometry_msgs.msg import TransformStamped


def yaw_to_quat(yaw):
    return (0.0, 0.0, math.sin(yaw / 2.0), math.cos(yaw / 2.0))


class ValetudoBridge(Node):
    def __init__(self, host, rate):
        super().__init__('valetudo_bridge')
        self.host = host.rstrip('/')
        # latched map (transient_local) so late subscribers (RViz) still get the last map
        map_qos = QoSProfile(depth=1,
                             reliability=QoSReliabilityPolicy.RELIABLE,
                             durability=QoSDurabilityPolicy.TRANSIENT_LOCAL)
        self.pub_map = self.create_publisher(OccupancyGrid, '/map', map_qos)
        self.pub_odom = self.create_publisher(Odometry, '/odom', 10)
        self.pub_status = self.create_publisher(String, '/robot/status', 10)
        self.tf = TransformBroadcaster(self)
        self.create_timer(1.0 / rate, self.tick)
        self.get_logger().info(f'valetudo_bridge polling {self.host} at {rate} Hz')

    def fetch(self, path):
        with urllib.request.urlopen(self.host + path, timeout=4) as r:
            return json.load(r)

    def tick(self):
        try:
            m = self.fetch('/api/v2/robot/state/map')
        except Exception as e:
            self.get_logger().warn(f'map fetch failed: {e}')
            return
        now = self.get_clock().now().to_msg()
        ps = m['pixelSize']                       # mm per pixel
        res = ps / 1000.0

        # --- OccupancyGrid over the floor+wall bounding box ---
        xs, ys = [], []
        for L in m.get('layers', []):
            d = L['dimensions']
            xs += [d['x']['min'], d['x']['max']]
            ys += [d['y']['min'], d['y']['max']]
        if xs:
            minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
            W, H = maxx - minx + 1, maxy - miny + 1
            grid = array('b', [-1]) * (W * H)
            for L in m.get('layers', []):
                val = 100 if L['type'] == 'wall' else 0
                cp = L.get('compressedPixels', []) or []
                for k in range(0, len(cp), 3):
                    x, y, cnt = cp[k], cp[k + 1], cp[k + 2]
                    r = maxy - y                  # ROS y-up: row 0 = bottom = maxy
                    if 0 <= r < H:
                        base = r * W + (x - minx)
                        for dx in range(cnt):
                            c = x - minx + dx
                            if 0 <= c < W:
                                i = base + dx
                                if val == 100 or grid[i] != 100:   # wall wins over floor
                                    grid[i] = val
            og = OccupancyGrid()
            og.header.stamp = now
            og.header.frame_id = 'map'
            og.info.resolution = res
            og.info.width = W
            og.info.height = H
            og.info.origin.position.x = minx * res
            og.info.origin.position.y = -maxy * res      # bottom row corresponds to max Valetudo-y
            og.info.origin.orientation.w = 1.0
            og.data = grid
            self.pub_map.publish(og)

        # --- robot pose -> /odom + TF ---
        rp = next((e for e in m.get('entities', []) if e.get('type') == 'robot_position'), None)
        if rp and rp.get('points'):
            x_mm, y_mm = rp['points'][0], rp['points'][1]
            ang = (rp.get('metaData') or {}).get('angle', 0)
            x, y = x_mm / 1000.0, -y_mm / 1000.0
            qz, qw = yaw_to_quat(math.radians(-ang))[2:]

            odom = Odometry()
            odom.header.stamp = now
            odom.header.frame_id = 'map'
            odom.child_frame_id = 'base_link'
            odom.pose.pose.position.x = x
            odom.pose.pose.position.y = y
            odom.pose.pose.orientation.z = qz
            odom.pose.pose.orientation.w = qw
            self.pub_odom.publish(odom)

            t = TransformStamped()
            t.header.stamp = now
            t.header.frame_id = 'map'
            t.child_frame_id = 'base_link'
            t.transform.translation.x = x
            t.transform.translation.y = y
            t.transform.rotation.z = qz
            t.transform.rotation.w = qw
            self.tf.sendTransform(t)

        # --- status (cheap, from attributes) ---
        try:
            attrs = self.fetch('/api/v2/robot/state/attributes')
            st = next((a for a in attrs if a.get('__class') == 'StatusStateAttribute'), None)
            if st:
                self.pub_status.publish(String(data=f"{st.get('value')}/{st.get('flag')}"))
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='http://127.0.0.1')
    ap.add_argument('--rate', type=float, default=1.0)
    a = ap.parse_args()
    rclpy.init()
    node = ValetudoBridge(a.host, a.rate)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
