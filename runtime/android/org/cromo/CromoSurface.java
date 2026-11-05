package org.cromo;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

// The surface of SDL, whose safe area also leaves out the keyboard on the screen
// (D471): std.gui puts its content above it, as adjustResize of Android does with
// the views. The insets come with Android 11; before it the window is shorter.
public class CromoSurface extends SDLSurface {
    public CromoSurface(Context context) {
        super(context);
    }

    @Override
    public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
        WindowInsets result = super.onApplyWindowInsets(view, insets);
        if (Build.VERSION.SDK_INT >= 30) {
            // The bars of the system and the cutout, with the keyboard when it is
            // higher. SDL also leaves out the areas of the gestures, for the games:
            // the content of an app goes under those at the sides (D456).
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            int bottom = bars.bottom;
            if (insets.isVisible(WindowInsets.Type.ime())) {
                bottom = Math.max(bottom, insets.getInsets(WindowInsets.Type.ime()).bottom);
            }
            SDLActivity.onNativeInsetsChanged(bars.left, bars.right, bars.top, bottom);
        }
        return result;
    }
}
