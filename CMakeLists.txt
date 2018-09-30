cmake_minimum_required (VERSION 2.6)
project (ATEMmonitor)

# force Unicode over Multi-byte
ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)

add_executable (ATEMmonitor ATEMmonitor.cpp Serial.cpp)