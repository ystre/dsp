# If line matches regex do the block,
# i.e., extract command name and check if it is the required command
# Note: `cmd` is defined via `awk -v cmd=<VALUE>`
/^\/\/ CMD:/ {
    split($0, a, /:\s/);
    found = (a[2] == cmd)
}

# Toogle printing between the Ascii doc code block markers
found && $0 ~ /^----$/ {
    capture = !capture;
    if (!capture) {
        exit
    }
    next
}

# Print text
capture
