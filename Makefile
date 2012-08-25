all:
	$(CC) -g -Wall -shared -fPIC -o libgstmythtvsrc.so gstmythtvsrc.c -lcmyth -lrefmem -DVERSION=0 -DPACKAGE=\"MythTV\" `pkg-config --cflags --libs gstreamer-0.10` `pkg-config --cflags --libs gstreamer-base-0.10` `mysql_config --libs`

cp:
	su -c "cp libgstmythtvsrc.so /usr/lib/x86_64-linux-gnu/gstreamer-0.10/libgstmythtvsrc.so"

clean:
	rm -f *.so
