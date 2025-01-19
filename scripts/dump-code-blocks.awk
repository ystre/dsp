# Toogle printing between the Ascii doc code block markers which are
# specified as bash.
$0 ~ /^\[source,bash\]$/ {
    bash = 1
    next
}

$0 ~ /^----$/ && bash {
    capture = !capture;
    if (!capture) {
        bash = false
        exit
    }
    next
}

# Print text
capture
