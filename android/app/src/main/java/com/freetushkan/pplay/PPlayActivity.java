//
// pPlay on Android: SDL runs the native part (libpplay.so, SDL_main). Here: the paths it
// asks for (libcross2d AndroidIo), the bundled files (assets/romfs) extracted once per
// install, the access to all files asked. The data (settings, logs, progress) in
// <storage>/.pplay: kept when the app is removed, reachable without root; the app's own
// directory if no access.
//

package com.freetushkan.pplay;

import android.Manifest;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.InputFilter;
import android.text.InputType;
import android.util.Log;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public class PPlayActivity extends SDLActivity {

    private static final String TAG = "pPlay";
    private static final String DATA_DIR = ".pplay";
    private static String dataPath = "";
    private static String fallbackPath = "";
    private static String romFsPath = "";
    private static PPlayActivity instance;
    // the access asked, the answer awaited (getDataPath waits for it)
    private static final Object accessLock = new Object();
    private static boolean asking = false;
    private static boolean leftForAccess = false;

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
        fallbackPath = data.getAbsolutePath();
        File romfs = new File(getFilesDir(), "romfs");
        romFsPath = romfs.getAbsolutePath();
        extractRomFs(romfs);
        askStorage();
        instance = this;
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
    private boolean hasStorage() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void askStorage() {
        if (hasStorage()) {
            return;
        }
        setAsking(true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception e2) {
                    Log.w(TAG, "no all files access settings", e2);
                    setAsking(false);
                }
            }
        } else {
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
        }
    }

    private static void setAsking(boolean value) {
        synchronized (accessLock) {
            asking = value;
            if (!value) {
                leftForAccess = false;
            }
            accessLock.notifyAll();
        }
    }

    @Override
    protected void onPause() {
        synchronized (accessLock) {
            if (asking) {
                leftForAccess = true;
            }
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // back from the access settings
        synchronized (accessLock) {
            if (asking && leftForAccess) {
                setAsking(false);
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        setAsking(false);
    }

    // the text input (src/app/keyboard.cpp, the native thread waits): the edited text, null
    // if cancelled
    public static String editText(final String title, final String text, final boolean multiline,
                                  final int max) {
        final PPlayActivity activity = instance;
        if (activity == null) {
            return null;
        }
        final AtomicReference<String> result = new AtomicReference<>(null);
        final CountDownLatch done = new CountDownLatch(1);
        activity.runOnUiThread(() -> {
            final EditText edit = new EditText(activity);
            edit.setText(text);
            edit.setSelection(edit.getText().length());
            edit.setFilters(new InputFilter[]{new InputFilter.LengthFilter(max)});
            if (multiline) {
                edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
                edit.setMinLines(3);
            } else {
                edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
                edit.setSingleLine(true);
                edit.setImeOptions(EditorInfo.IME_ACTION_DONE);
            }
            final AlertDialog dialog = new AlertDialog.Builder(activity)
                    .setTitle(title)
                    .setView(edit)
                    .setPositiveButton(android.R.string.ok,
                            (d, w) -> result.set(edit.getText().toString()))
                    .setNegativeButton(android.R.string.cancel, null)
                    .create();
            dialog.setOnDismissListener(d -> done.countDown());
            if (!multiline) {
                // the keyboard's "Done": as OK
                edit.setOnEditorActionListener((v, action, event) -> {
                    if (action == EditorInfo.IME_ACTION_DONE) {
                        result.set(edit.getText().toString());
                        dialog.dismiss();
                        return true;
                    }
                    return false;
                });
            }
            if (dialog.getWindow() != null) {
                dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            }
            dialog.show();
            edit.requestFocus();
        });
        try {
            done.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return result.get();
    }

    // libcross2d (AndroidIo, ANDROIDDevice)
    // the native thread, once: after the access answered (5 minutes at most)
    public static String getDataPath() {
        synchronized (accessLock) {
            long until = System.currentTimeMillis() + 5 * 60 * 1000;
            while (asking && System.currentTimeMillis() < until) {
                try {
                    accessLock.wait(1000);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        if (dataPath.isEmpty()) {
            File data = new File(Environment.getExternalStorageDirectory(), DATA_DIR);
            boolean granted = instance != null && instance.hasStorage();
            if (granted && (data.isDirectory() || data.mkdirs())) {
                dataPath = data.getAbsolutePath();
            } else {
                Log.w(TAG, "no access to " + data + ", data in " + fallbackPath);
                dataPath = fallbackPath;
            }
        }
        return dataPath;
    }

    public static String getRomFsPath() {
        return romFsPath;
    }

    public static String getDeviceName() {
        return Build.MANUFACTURER + " " + Build.MODEL;
    }
}
