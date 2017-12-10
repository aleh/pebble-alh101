
case "$1" in
phone* )
	TARGET="--phone 192.168.1.100" ;;
cloud* )
	TARGET="--cloudpebble" ;;
emu*)
	TARGET="--emulator basalt" ;;
*)
	echo "$0 [phone|cloud|emu]"
	exit ;;
esac

pebble build && pebble install -v --logs $TARGET 
