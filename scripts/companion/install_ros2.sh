#!/bin/bash
# Install ROS 2 Jazzy on Radxa Dragon Q6A (Debian 12 / Ubuntu 24.04 arm64)
set -e

echo "=== Setting locale ==="
apt-get install -y locales
locale-gen en_US en_US.UTF-8
update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8

echo "=== Adding ROS 2 apt repository ==="
apt-get install -y curl software-properties-common
curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=arm64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
  http://packages.ros.org/ros2/ubuntu noble main" \
  > /etc/apt/sources.list.d/ros2.list

echo "=== Installing ROS 2 Jazzy base + Nav2 ==="
apt-get update
apt-get install -y \
  ros-jazzy-ros-base \
  ros-jazzy-nav2-bringup \
  ros-jazzy-nav2-msgs \
  ros-jazzy-sensor-msgs \
  ros-jazzy-geometry-msgs \
  ros-jazzy-tf2-ros \
  python3-colcon-common-extensions \
  python3-rosdep

echo "=== Initialising rosdep ==="
rosdep init || true
rosdep update

echo "=== Adding ROS setup to .bashrc ==="
echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc

echo "Done. Run: source /opt/ros/jazzy/setup.bash"
