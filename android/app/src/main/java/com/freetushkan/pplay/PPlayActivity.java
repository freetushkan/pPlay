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
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.IntentFilter;
import android.os.Build;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
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
        takeView(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        takeView(intent);
    }

    // VIEW intent: "<url>\n<name>"
    private static volatile String viewed = null;

    private void takeView(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction()) || intent.getData() == null) {
            return;
        }
        Uri uri = intent.getData();
        String name = uri.getLastPathSegment() != null ? uri.getLastPathSegment() : uri.toString();
        String url = uri.toString();
        if ("content".equals(uri.getScheme())) {
            try (Cursor c = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst() && c.getString(0) != null) {
                    name = c.getString(0);
                }
            } catch (Exception ignored) {
            }
            try {
                ParcelFileDescriptor fd = getContentResolver().openFileDescriptor(uri, "r");
                if (fd == null) {
                    return;
                }
                url = "fdclose://" + fd.detachFd();
            } catch (Exception e) {
                Log.e(TAG, "can't open " + uri, e);
                return;
            }
        } else if ("file".equals(uri.getScheme())) {
            url = uri.getPath();
        }
        if (intent.getStringExtra("title") != null) {
            name = intent.getStringExtra("title");
        }
        viewed = url + "\n" + name;
    }

    // media session; commands polled by the native side
    private static MediaSession session = null;
    private static final java.util.concurrent.ConcurrentLinkedQueue<String> commands =
            new java.util.concurrent.ConcurrentLinkedQueue<>();

    public static String takeMediaCommand() {
        return commands.poll();
    }

    public static void mediaState(final String title, final int state, final long position, final long duration) {
        final PPlayActivity activity = instance;
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            if (session == null) {
                session = new MediaSession(activity, "pPlay");
                session.setSessionActivity(PendingIntent.getActivity(activity, 0,
                        new Intent(activity, PPlayActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
                        PendingIntent.FLAG_IMMUTABLE));
                session.setCallback(new MediaSession.Callback() {
                    @Override public void onPlay() { commands.add("play"); }
                    @Override public void onPause() { commands.add("pause"); }
                    @Override public void onStop() { commands.add("stop"); }
                    @Override public void onSeekTo(long ms) { commands.add("seek " + ms); }
                });
            }
            if (state == 0) {
                session.setActive(false);
                activity.audioFocus(false);
                activity.notifyMedia(null, false);
                return;
            }
            // audio focus makes this the active session (shared by Google TV with phones)
            if (state == 2) {
                activity.audioFocus(true);
            }
            session.setMetadata(new MediaMetadata.Builder()
                    .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                    .putString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE, title)
                    .putLong(MediaMetadata.METADATA_KEY_DURATION, duration).build());
            session.setPlaybackState(new PlaybackState.Builder()
                    .setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE
                            | PlaybackState.ACTION_STOP | PlaybackState.ACTION_SEEK_TO)
                    .setState(state == 2 ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED, position,
                            state == 2 ? 1.0f : 0.0f).build());
            session.setActive(true);
            activity.notifyMedia(title, state == 2);
        });
    }

    private Object focusRequest = null;
    private AudioManager.OnAudioFocusChangeListener focusListener = null;
    private boolean focused = false;

    private void audioFocus(boolean gain) {
        AudioManager audio = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (audio == null || gain == focused) {
            return;
        }
        if (focusListener == null) {
            focusListener = change -> {
                if (change == AudioManager.AUDIOFOCUS_LOSS || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
                    focused = change != AudioManager.AUDIOFOCUS_LOSS;
                    commands.add("pause");
                }
            };
        }
        AudioManager.OnAudioFocusChangeListener listener = focusListener;
        if (Build.VERSION.SDK_INT >= 26) {
            if (focusRequest == null) {
                focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                        .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                                .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build())
                        .setOnAudioFocusChangeListener(listener).build();
            }
            if (gain) {
                focused = audio.requestAudioFocus((AudioFocusRequest) focusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
            } else {
                audio.abandonAudioFocusRequest((AudioFocusRequest) focusRequest);
                focused = false;
            }
        } else if (gain) {
            focused = audio.requestAudioFocus(listener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
                      == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        } else {
            audio.abandonAudioFocus(listener);
            focused = false;
        }
    }

    // media notification
    private static final String MEDIA_ACTION = "com.freetushkan.pplay.MEDIA";
    private static final int MEDIA_NOTIFICATION = 1;
    private boolean mediaReceiver = false;
    private boolean notificationsAsked = false;

    @Override
    protected void onDestroy() {
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.cancel(MEDIA_NOTIFICATION);
        }
        super.onDestroy();
    }

    private PendingIntent mediaIntent(String command, int code) {
        Intent intent = new Intent(MEDIA_ACTION).setPackage(getPackageName()).putExtra("command", command);
        return PendingIntent.getBroadcast(this, code, intent, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private void notifyMedia(String title, boolean playing) {
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) {
            return;
        }
        if (title == null) {
            manager.cancel(MEDIA_NOTIFICATION);
            return;
        }
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            if (!notificationsAsked) {
                notificationsAsked = true;
                requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 3);
            }
            return;
        }
        if (!mediaReceiver) {
            mediaReceiver = true;
            BroadcastReceiver receiver = new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    String command = intent.getStringExtra("command");
                    if (command != null) {
                        commands.add(command);
                    }
                }
            };
            if (Build.VERSION.SDK_INT >= 33) {
                registerReceiver(receiver, new IntentFilter(MEDIA_ACTION), Context.RECEIVER_NOT_EXPORTED);
            } else {
                registerReceiver(receiver, new IntentFilter(MEDIA_ACTION));
            }
        }
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= 26) {
            manager.createNotificationChannel(new NotificationChannel("playback", "Playback", NotificationManager.IMPORTANCE_LOW));
            builder = new Notification.Builder(this, "playback");
        } else {
            builder = new Notification.Builder(this);
        }
        Intent open = new Intent(this, PPlayActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        builder.setSmallIcon(playing ? android.R.drawable.ic_media_play : android.R.drawable.ic_media_pause)
                .setContentTitle(title)
                .setContentIntent(PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE))
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOngoing(playing)
                .setShowWhen(false)
                .addAction(new Notification.Action.Builder(
                        playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play,
                        playing ? "Pause" : "Play", mediaIntent(playing ? "pause" : "play", 1)).build())
                .addAction(new Notification.Action.Builder(android.R.drawable.ic_menu_close_clear_cancel,
                        "Stop", mediaIntent("stop", 2)).build())
                .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken())
                        .setShowActionsInCompactView(0, 1));
        manager.notify(MEDIA_NOTIFICATION, builder.build());
    }

    public static String takeViewed() {
        String v = viewed;
        viewed = null;
        return v;
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
        long start = System.currentTimeMillis();
        deleteTree(romfs);
        try {
            copyAssets(getAssets(), "romfs", romfs);
            prefs.edit().putLong("romfs", installed).apply();
            try (OutputStream out = new FileOutputStream(new File(getFilesDir(), "romfs.time"))) {
                out.write(Long.toString(System.currentTimeMillis() - start).getBytes());
            }
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
