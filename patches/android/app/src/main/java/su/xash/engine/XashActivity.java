package su.xash.engine;
import android.app.Activity; import android.os.Bundle;
import android.os.Environment; import android.view.WindowManager;
public class XashActivity extends Activity {
    static { try { System.loadLibrary("xash"); } catch(UnsatisfiedLinkError e){} }
    private native void nativeInit(String g, String d);
    private native void nativeQuit();
    @Override protected void onCreate(Bundle s) {
        super.onCreate(s);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON|
                            WindowManager.LayoutParams.FLAG_FULLSCREEN);
        nativeInit(Environment.getExternalStorageDirectory()+"/xash3d",
                   getFilesDir().getAbsolutePath());
    }
    @Override protected void onDestroy() { super.onDestroy(); try{nativeQuit();}catch(Throwable t){} }
}
