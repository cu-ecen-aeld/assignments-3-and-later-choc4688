


case "$1" in   
    start)
        echo "Loading aesdchar"
        ./usr/bin/aesdchar_load
        ;;
    stop)
        echo "Unloading aesdchar"
        ./usr/bin/aesdchar_unload
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

