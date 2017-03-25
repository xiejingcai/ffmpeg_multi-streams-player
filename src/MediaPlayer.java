import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.image.BufferedImage;
import javax.swing.JFrame;
import javax.swing.JPanel;

public class MediaPlayer extends JPanel  {

	public native int init();
	public native int exit();
	public native int open(byte[] url,int len);
	public native int close(int id);
	public native int getwidth(int id);
	public native int getheight(int id);
	public native int getframe(int id, byte[] buffer);

	private BufferedImage canvas;
	protected Thread thread0;
	protected Thread thread1;
	protected Thread thread2;
	protected Thread thread3;
	private static int running;
	private static int width = 1280;
	private static int height = 720;

	static {
		System.loadLibrary("mediaplayer");
	}

	public MediaPlayer(int width, int height) {
		running = 0;
		canvas = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
	}

	public Dimension getPreferredSize() {
		return new Dimension(canvas.getWidth(), canvas.getHeight());
	}

	public void paintComponent(Graphics g) {
		super.paintComponent(g);
		Graphics2D g2 = (Graphics2D) g;
		g2.drawImage(canvas, null, null);
	}
	public int makeColor(int r,int g, int b){
		return 0xff000000|(r<<16)|(g<<8)|b;
	}
	public void cleanCanvas(){
		int x,y;
		int c = makeColor(0,0,0);
		for(y=0;y<height;y++){
			for(x=0;x<width;x++){
				canvas.setRGB(x,y,c);
			}
		}
		repaint();
	}

	public void fillCanvasWithImage(byte[] buffer,int w,int h,int area){
		int color = 0,offset = 0;
		int x_a=0,y_a=0;
		int x_scale = 0,y_scale = 0;
		switch(area){
			case 0:
				x_a = 0;
				y_a = 0;
				break;
			case 1:
				x_a = width/2;
				y_a = 0;
				break;
			case 2:
				x_a = 0;
				y_a = height/2;
				break;
			case 3:
				x_a = width/2;
				y_a = height/2;
				break;
		}
		for (int x = 0; x < width/2; x++) {
			for (int y = 0; y < height/2; y++) {
				color = 0;
				x_scale = x*w/(width/2);
				y_scale = y*h/(height/2);
				offset = (x_scale+y_scale*w)*4;
				color = (int) ((buffer[offset+2] & 0xFF)   
						| ((buffer[offset+1] & 0xFF)<<8)   
						| ((buffer[offset+0] & 0xFF)<<16)   
						| ((buffer[offset+3] & 0xFF)<<24)); 

				canvas.setRGB(x+x_a, y+y_a, color);
			}
		}
		repaint();  
	}
	public Thread playback(String url_s,final int area){

		byte[] url = url_s.getBytes();
		final int mpi_id = open(url,url.length); 
		final byte[] buffer = new byte[1920*1080*4];
		Thread thread = new Thread(
					new Runnable(){
						public void run(){
							int ret=0,w=0,h=0;
							while(running == 1){
								ret = getframe(mpi_id,buffer);
								if(ret>0){
									w = getwidth(mpi_id);
									h = getheight(mpi_id);
									fillCanvasWithImage(buffer,w,h,area);
								}
								try {
									Thread.sleep(10);
								} catch (InterruptedException e) {
									e.printStackTrace();
								}
							}					
							close(mpi_id);
						}  
					}
				); 

		thread.start();

		return thread; 
	}

	public static void main(String[] args) {

		final JFrame frame = new JFrame("MediaPlayer");	    
		final MediaPlayer panel = new MediaPlayer(width, height);		

		frame.add(panel);    
		frame.pack();
		frame.setVisible(true);
		frame.setResizable(true);
		frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);

		panel.cleanCanvas();
		frame.addMouseListener(
			new MouseAdapter(){
				public void mouseClicked(MouseEvent e){
					if(e.getClickCount()==2){
						if(running == 0){
							running = 1;
							System.out.println("Start !!!");	  
							panel.init();
							panel.thread0=panel.playback("rtmp://192.168.1.108:1935/rtmp/live1",0);
							panel.thread1=panel.playback("rtmp://192.168.1.108:1935/rtmp/live1",1);
							panel.thread2=panel.playback("rtmp://192.168.1.108:1935/rtmp/live1",2);
							panel.thread3=panel.playback("rtmp://192.168.1.108:1935/rtmp/live1",3);
						}else{
							running = 0;
							try {
								panel.thread0.join();
							} catch (InterruptedException e1) {
								e1.printStackTrace();
							}
							try {
								panel.thread1.join();
							} catch (InterruptedException e1) {
								e1.printStackTrace();
							}
							try {
								panel.thread2.join();
							} catch (InterruptedException e1) {
								e1.printStackTrace();
							}
							try {
								panel.thread3.join();
							} catch (InterruptedException e1) {
								e1.printStackTrace();
							}
							panel.cleanCanvas();
							panel.exit();
							System.out.println("Stop !!!");
						}
					}
				}
			}
		);
	}
}
