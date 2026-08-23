// The whole Java side of Forcetris: SDL's stock activity, told which
// native libraries to stand up. Everything else is C++.
package net.kjh.forcetris;

import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }
}
