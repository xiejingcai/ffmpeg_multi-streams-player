c_cc = gcc  
j_cc = javac 
so = lib/libmediaplayer.so  
c_src = native/mediaplayer.c
inc = -I./include/ -I/usr/lib/jvm/jdk1.8.0_111/include/ -I/usr/lib/jvm/jdk1.8.0_111/include/linux/
link = -lavformat -lavcodec -lavutil -lswscale -pthread
lib = -L./lib  
  
$(so): 
	$(c_cc) $(c_src) -o $(so) -shared -fPIC $(inc) $(link) $(lib)

clean:  
	rm -rf $(so)  
