package com.magebros.game;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The launcher: checks for the player's own game files, explains what is missing and copies a
 * chosen folder into the app's data root. It is deliberately Java-only and runs before SDL, so
 * the game never starts without its files.
 *
 * The copy keeps the exact names the game expects, writes each file through a temporary name and
 * renames it only after the bytes are on disk. Only MageBros.exe, Music01..06.mid and an optional
 * .sf2 are copied; everything else in the chosen folder is ignored.
 */
public class GameFilesActivity extends Activity {
    private static final String TAG = "MageBrosImport";
    private static final int REQUEST_PICK_FOLDER = 41;

    static {
        // The verification lives in the game library so there is one known-build table.
        System.loadLibrary("main");
    }

    private static native String nativeVerifyExecutable(String path);

    private static native String nativeTargetName(String name);

    private File gameDir;
    private TextView status;
    private ProgressBar progress;
    private Button choose;
    private boolean importOnly;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setBackgroundDrawable(new ColorDrawable(0xFF10141A));
        gameDir = resolveGameDir();
        importOnly = getIntent().getBooleanExtra("importOnly", false);
        buildUi();

        String importPath = getIntent().getStringExtra("importPath");
        if (importPath != null) {
            importFromFolder(new File(importPath));
            return;
        }
        if (importOnly) {
            status.setText("Choose a folder to replace the game files. The next start uses them.");
        } else if (findExecutable() != null) {
            verifyAndStart();
        }
    }

    private File resolveGameDir() {
        File base = getExternalFilesDir(null);
        if (base == null) base = getFilesDir();
        File dir = new File(base, "MageBrosNative/game");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            Log.w(TAG, "could not create " + dir);
        }
        return dir;
    }

    private File findExecutable() {
        File[] files = gameDir.listFiles();
        if (files == null) return null;
        for (File f : files)
            if (f.isFile() && f.getName().equalsIgnoreCase("MageBros.exe")) return f;
        return null;
    }

    private void buildUi() {
        int pad = dp(28);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        column.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Mage Bros. Native");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        title.setGravity(Gravity.CENTER);
        column.addView(title);

        TextView body = new TextView(this);
        body.setText("This app contains none of the original game.\n\n"
                + "Choose the folder that holds MageBros.exe and Music01.mid to Music06.mid. "
                + "You need your own copy of the freeware Mage Bros. 2.0; a SoundFont (.sf2) "
                + "in the same folder is copied too.");
        body.setTextColor(0xFFD7DEE8);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        body.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams bodyParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        bodyParams.topMargin = dp(18);
        column.addView(body, bodyParams);

        choose = new Button(this);
        choose.setText("Choose game folder");
        choose.setOnClickListener(v -> pickFolder());
        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        buttonParams.topMargin = dp(24);
        column.addView(choose, buttonParams);

        if (importOnly) {
            Button close = new Button(this);
            close.setText("Close");
            close.setOnClickListener(v -> finish());
            column.addView(close, buttonParams);
        }

        progress = new ProgressBar(this);
        progress.setIndeterminate(true);
        progress.setVisibility(ViewGroup.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(dp(48), dp(48));
        progressParams.topMargin = dp(16);
        column.addView(progress, progressParams);

        status = new TextView(this);
        status.setTextColor(0xFFE8C97A);
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        statusParams.topMargin = dp(12);
        column.addView(status, statusParams);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(column);
        setContentView(scroll);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void pickFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, REQUEST_PICK_FOLDER);
        } catch (Exception e) {
            status.setText("No file manager is available to choose a folder.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_FOLDER) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            status.setText("No folder chosen. Choose the folder with MageBros.exe when you are ready.");
            return;
        }
        Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                    tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
            // The copy below still runs while the temporary grant is alive.
        }
        importFromTree(tree);
    }

    private void beginImport() {
        choose.setEnabled(false);
        progress.setVisibility(ViewGroup.VISIBLE);
        status.setText("Importing game files...");
    }

    private void finishImport(boolean ok, String message) {
        progress.setVisibility(ViewGroup.GONE);
        choose.setEnabled(true);
        status.setText(message);
        if (ok) verifyAndStart();
    }

    /** Refuses an unknown build before the game starts; the C++ check is the only known table. */
    private void verifyAndStart() {
        File executable = findExecutable();
        if (executable == null) {
            status.setText("MageBros.exe was not found in the game folder.");
            return;
        }
        choose.setEnabled(false);
        progress.setVisibility(ViewGroup.VISIBLE);
        status.setText("Checking the game files...");
        final String path = executable.getAbsolutePath();
        new Thread(() -> {
            String verdict;
            try {
                verdict = nativeVerifyExecutable(path);
            } catch (Throwable t) {
                verdict = "ERROR:" + t;
            }
            final String text = verdict;
            runOnUiThread(() -> handleVerdict(text));
        }, "game-files-verify").start();
    }

    private void handleVerdict(String verdict) {
        progress.setVisibility(ViewGroup.GONE);
        choose.setEnabled(true);
        if (verdict.startsWith("OK:")) {
            Log.i(TAG, "verified build: " + verdict.substring(3));
            if (importOnly) status.setText("Game files imported. Restart the game to use them.");
            else startGame(false);
        } else if (verdict.startsWith("UNKNOWN:")) {
            showUnknownBuild(verdict.substring(8));
        } else {
            status.setText(verdict.startsWith("ERROR:") ? verdict.substring(6) : verdict);
        }
    }

    private void showUnknownBuild(String hash) {
        status.setText("That MageBros.exe is not a build this port knows.");
        new AlertDialog.Builder(this)
                .setTitle("Unknown game build")
                .setMessage("This MageBros.exe is not a build this port knows.\n\nSHA-256: " + hash
                        + "\n\nStart it anyway? It may not work correctly.")
                .setPositiveButton("Start anyway", (dialog, which) -> {
                    if (importOnly)
                        status.setText("Game files imported (unverified build). Restart the game to use them.");
                    else
                        startGame(true);
                })
                .setNegativeButton("Choose another folder", (dialog, which) -> {
                    File executable = findExecutable();
                    if (executable != null) executable.delete();
                    pickFolder();
                })
                .setCancelable(false)
                .show();
    }

    private void startGame(boolean allowUnverified) {
        Intent intent = new Intent(this, MageBrosActivity.class);
        intent.putExtra("allowUnverified", allowUnverified);
        String auto = getIntent().getStringExtra("autoArgs");
        if (auto != null) intent.putExtra("autoArgs", auto);
        if (getIntent().getBooleanExtra("benchmark", false)) {
            intent.putExtra("benchmark", true);
            String seconds = getIntent().getStringExtra("benchmarkSeconds");
            if (seconds != null) intent.putExtra("benchmarkSeconds", seconds);
            String report = getIntent().getStringExtra("benchmarkReport");
            if (report != null) intent.putExtra("benchmarkReport", report);
        }
        startActivity(intent);
        finish();
    }

    private void importFromTree(Uri tree) {
        beginImport();
        new Thread(() -> {
            Result result = new Result();
            ContentResolver resolver = getContentResolver();
            try {
                Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                        tree, DocumentsContract.getTreeDocumentId(tree));
                String[] columns = {
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE,
                };
                try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
                    if (cursor == null) {
                        finishOnUi(false, "That folder could not be read.");
                        return;
                    }
                    while (cursor.moveToNext()) {
                        String id = cursor.getString(0);
                        String name = cursor.getString(1);
                        String mime = cursor.getString(2);
                        if (name == null || DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) continue;
                        String target = targetName(name);
                        if (target == null) continue;
                        Uri file = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                        try (InputStream in = resolver.openInputStream(file)) {
                            copyOne(in, target, result);
                        } catch (Exception e) {
                            result.failed.add(name);
                            Log.w(TAG, "could not read " + name, e);
                        }
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "folder query failed", e);
                finishOnUi(false, "That folder could not be read: " + e.getMessage());
                return;
            }
            finishOnUi(result.ok(), result.message());
        }, "game-files-import").start();
    }

    /** Test hook: imports a plain folder (adb shell am start ... --es importPath /sdcard/x). */
    private void importFromFolder(File folder) {
        beginImport();
        new Thread(() -> {
            Result result = new Result();
            File[] files = folder.listFiles();
            if (files == null) {
                finishOnUi(false, "That folder does not exist or cannot be read.");
                return;
            }
            for (File file : files) {
                String target = targetName(file.getName());
                if (target == null || !file.isFile()) continue;
                try (InputStream in = new FileInputStream(file)) {
                    copyOne(in, target, result);
                } catch (Exception e) {
                    result.failed.add(file.getName());
                    Log.w(TAG, "could not read " + file.getName(), e);
                }
            }
            finishOnUi(result.ok(), result.message());
        }, "game-files-import").start();
    }

    /** The name in the game folder for an imported file, or null when it is not one we copy. */
    private String targetName(String displayName) {
        String target = nativeTargetName(displayName);
        return target.isEmpty() ? null : target;
    }

    private void copyOne(InputStream in, String targetName, Result result) throws IOException {
        File temp = new File(gameDir, targetName + ".part");
        File dest = new File(gameDir, targetName);
        try (OutputStream out = new FileOutputStream(temp)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
            out.flush();
            if (out instanceof FileOutputStream) ((FileOutputStream) out).getFD().sync();
        } catch (IOException e) {
            temp.delete();
            throw e;
        }
        if (dest.exists() && !dest.delete()) {
            temp.delete();
            throw new IOException("could not replace " + dest);
        }
        if (!temp.renameTo(dest)) {
            temp.delete();
            throw new IOException("could not rename " + temp + " to " + dest);
        }
        result.imported.add(targetName);
        Log.i(TAG, "imported " + targetName + " to " + gameDir);
    }

    private void finishOnUi(boolean ok, String message) {
        runOnUiThread(() -> finishImport(ok, message));
    }

    /** The outcome of one import, built on the worker thread. */
    private static final class Result {
        final List<String> imported = new ArrayList<>();
        final List<String> failed = new ArrayList<>();

        boolean ok() {
            return imported.stream().anyMatch(n -> n.equalsIgnoreCase("MageBros.exe"));
        }

        String message() {
            if (!ok()) return "MageBros.exe was not found in that folder.";
            int music = 0;
            for (String name : imported)
                if (name.toLowerCase(Locale.US).endsWith(".mid")) music++;
            StringBuilder text = new StringBuilder("Imported: ").append(String.join(", ", imported)).append('.');
            if (music < 6) text.append(" Music files not found; the game will start without music.");
            if (!failed.isEmpty())
                text.append(" Could not copy: ").append(String.join(", ", failed)).append('.');
            return text.toString();
        }
    }
}
