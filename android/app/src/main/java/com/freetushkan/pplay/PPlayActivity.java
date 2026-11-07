//
// pPlay on Android: SDL runs the native part (libpplay.so, SDL_main). Here: the paths it
// asks for (libcross2d AndroidIo), the bundled files (assets/romfs) extracted once per
// install, the access to all files asked.
//

package com.freetushkan.pplay;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class PPlayActivity extends SDLActivity {

    private static final String TAG = "pPlay";
    private static String dataPath = "";
    private static String romFsPath = "";

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "pplay"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File data = getExternalFilesDir(null);
        if (data == null) {
            data = getFilesDir();
        }
        dataPath = data.getAbsolutePath();
        File romfs = new File(getFilesDir(), "romfs");
        romFsPath = romfs.getAbsolutePath();
        extractRomFs(romfs);
        askStorage();
        super.onCreate(savedInstanceState);
    }

    // a new install or update: the bundled files again
    private void extractRomFs(File romfs) {
        long installed;
        try {
            installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            installed = 0;
        }
        SharedPreferences prefs = getSharedPreferences("pplay", MODE_PRIVATE);
        if (romfs.isDirectory() && prefs.getLong("romfs", -1) == installed) {
            return;
        }
        deleteTree(romfs);
        try {
            copyAssets(getAssets(), "romfs", romfs);
            prefs.edit().putLong("romfs", installed).apply();
        } catch (IOException e) {
            Log.e(TAG, "romfs not extracted", e);
        }
    }

    private static void copyAssets(AssetManager assets, String path, File target) throws IOException {
        String[] names = assets.list(path);
        if (names == null || names.length == 0) {
            // a file (an empty directory too: nothing to copy then)
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[65536];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    out.write(buffer, 0, n);
                }
            } catch (IOException e) {
                if (!target.mkdirs() && !target.isDirectory()) {
                    throw e;
                }
            }
            return;
        }
        if (!target.mkdirs() && !target.isDirectory()) {
            throw new IOException("can't create " + target);
        }
        for (String name : names) {
            copyAssets(assets, path + "/" + name, new File(target, name));
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    // all files (the local files, the saved copies, the playlists): Android 11+ asks in its
    // settings, older ones by the permissions
    private void askStorage() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                return;
            }
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception e2) {
                    Log.w(TAG, "no all files access settings", e2);
                }
            }
        } else if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
        }
    }

    // libcross2d (AndroidIo, ANDROIDDevice)
    public static String getDataPath() {
        return dataPath;
    }

    public static String getRomFsPath() {
        return romFsPath;
    }

    public static String getDeviceName() {
        return Build.MANUFACTURER + " " + Build.MODEL;
    }
}
