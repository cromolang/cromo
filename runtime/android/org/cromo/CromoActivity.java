package org.cromo;

import android.os.Build;
import android.view.View;
import android.view.Window;

import org.libsdl.app.SDLActivity;

// The activity of the apps of Cromo (D452): that of SDL, which also colors the bars
// of the system as the bars of std.gui, with their icons dark on a light color
// (D456). std.gui sends the color with SDL_SendAndroidMessage when the window
// opens and when its theme changes; SDL gives the message here, on the thread of
// the interface.
public class CromoActivity extends SDLActivity {
    // The color in the lowest 24 bits, and 1 << 24 for dark icons.
    static final int COMMAND_SYSTEM_BARS = COMMAND_USER + 1;

    @Override
    protected boolean onUnhandledMessage(int command, Object param) {
        if (command != COMMAND_SYSTEM_BARS || !(param instanceof Integer)) {
            return super.onUnhandledMessage(command, param);
        }
        int value = (Integer) param;
        Window window = getWindow();
        window.setStatusBarColor(0xFF000000 | (value & 0xFFFFFF));
        window.setNavigationBarColor(0xFF000000 | (value & 0xFFFFFF));
        int dark = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
        if (Build.VERSION.SDK_INT >= 26) {
            dark |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
        }
        View decor = window.getDecorView();
        int flags = decor.getSystemUiVisibility();
        decor.setSystemUiVisibility((value & 0x1000000) != 0 ? flags | dark : flags & ~dark);
        return true;
    }
}
