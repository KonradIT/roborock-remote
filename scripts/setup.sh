#!/bin/bash
# Usage: ./setup.sh <email>
# Example: ./setup.sh hi@konraditurbe.dev

roborock login --email $1
roborock discover

echo "All set! Roborock discovered and logged in."