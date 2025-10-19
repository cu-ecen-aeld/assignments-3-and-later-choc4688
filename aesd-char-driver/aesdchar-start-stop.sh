


case "$1" in   
    start)
        echo "Loading aesdchar"
        modprobe aesdchar || exit 1
        ;;
    stop)
        echo "Unloading aesdchar"
        rmmod aesdchar || exit 1
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

