#!/bin/bash

# ==============================================================================
# Software Environment Setup Script
#
# This script checks for the existence of required environment variables and
# directories. It takes one argument: the base path for the directories.
# It prints a status message for each check:
# - Green text for success (item exists).
# - Red text for an error (item does not exist).
# ==============================================================================

# --- Color Definitions ---
# Use ANSI escape codes to define colors for output messages.
COLOR_GREEN='\033[0;32m'
COLOR_RED='\033[0;31m'
COLOR_NC='\033[0m' # No Color

# --- Script Input Validation ---
# Check if the user provided a base path for the directories.
if [ -z "$1" ]; then
    echo -e "${COLOR_RED}✖ Error:${COLOR_NC} No directory path provided."
    echo "Usage: $0 <path_to_directories>"
    exit 1
fi

BASE_PATH="$1"

# --- Helper Function for Printing Status ---
# This function checks an item and prints a formatted status message.
# Arguments:
#   $1: The name of the item being checked (e.g., "WD_BASEDIR").
#   $2: The type of check ('var' for variable, 'dir' for directory).
#   $3: The value of the item (only for variables).
check_item() {
    local item_name="$1"
    local check_type="$2"
    local item_value="$3"

    if [ "$check_type" == "var" ]; then
        if [ -n "$item_value" ]; then
            printf "${COLOR_GREEN}✔ Success:${COLOR_NC} Environment variable '%s' is set to: %s\n" "$item_name" "$item_value"
        else
            printf "${COLOR_RED}✖ Error:${COLOR_NC} Environment variable '%s' is not set.\n" "$item_name"
        fi
    elif [ "$check_type" == "dir" ]; then
        if [ -d "$item_name" ]; then
            printf "${COLOR_GREEN}✔ Success:${COLOR_NC} Directory '%s' exists.\n" "$item_name"
        else
            printf "${COLOR_RED}✖ Error:${COLOR_NC} Directory '%s' does not exist.\n" "$item_name"
        fi
    fi
}

# --- Main Script Logic ---

echo "--- Checking Environment Variables ---"
check_item "WD_BASEDIR" "var" "$WD_BASEDIR"
check_item "WD_KERNEL_MODULE_NAME" "var" "$WD_KERNEL_MODULE_NAME"

echo "" # Add a newline for better readability

echo "--- Checking Directories under '${BASE_PATH}' ---"
check_item "${BASE_PATH}/readout_data" "dir"
check_item "${BASE_PATH}/sabertooth_pps" "dir"
check_item "${BASE_PATH}/trigger_data" "dir"
check_item "${BASE_PATH}/logs" "dir"

echo ""
echo "Environment check complete."

