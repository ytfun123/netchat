# ----------------------------
# Makefile Options
# ----------------------------

NAME = NETCHAT
ICON = icon.png
DESCRIPTION = "Chat over USB, relayed by the flasher site"
COMPRESSED = NO

# Fixed protocol identifier the browser side checks for during the
# USB handshake. Bump this (and the matching string in the browser code)
# any time the wire protocol changes in an incompatible way.
CFLAGS = -Wall -Wextra -Oz -DLINKLIB_KEY="\"netcalc-chat-v1\""
CXXFLAGS = -Wall -Wextra -Oz -DLINKLIB_KEY="\"netcalc-chat-v1\""

# ----------------------------

include $(shell cedev-config --makefile)
