#!/bin/bash
export LD_LIBRARY_PATH=/users/blissadm/tango/redhate5_64/lib/:/users/blissadm/driver/redhate5/lib/x86_64/:/users/opid00/DeviceServers:$LD_LIBRARY_PATH
./DeviceServers/ds_LimaDetector maxipix.det -v1
