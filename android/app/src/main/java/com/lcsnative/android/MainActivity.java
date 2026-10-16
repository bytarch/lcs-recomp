package com.lcsnative.android;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Hosts the native LCSNative build.
 *
 * The game files (a decrypted EBOOT.ELF and the PSP_GAME folder) are not
 * shipped with the app. Until they are in the app's internal storage this
 * activity shows an installer screen; picking a folder copies it into internal
 * storage, after which the SDL activity starts.
 */
public class MainActivity extends SDLActivity {
    private static final String TAG = "LCSNative";
    private static final String GAME_DIR = "game";
    private static final int REQ_PICK_TREE = 4711;
    private static final String[] WANTED = {"EBOOT.ELF", "PSP_GAME"};

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (gameFilesReady()) {
            super.onCreate(savedInstanceState);
            return;
        }
        // Defer SDL startup: the native side aborts without the game files.
        showInstaller();
    }

    private File gameDir() {
        return new File(getFilesDir(), GAME_DIR);
    }

    private boolean gameFilesReady() {
        File dir = gameDir();
        for (String name : WANTED) {
            if (!new File(dir, name).exists()) {
                return false;
            }
        }
        return true;
    }

    // ------------------------------------------------------------ installer --

    private void showInstaller() {
        int pad = Math.round(24 * getResources().getDisplayMetrics().density);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("GTA: Liberty City Stories");
        title.setTextSize(22);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView body = new TextView(this);
        body.setText("This port needs your own game files.\n\n"
                + "Pick the folder that holds the decrypted EBOOT.ELF and the "
                + "PSP_GAME folder (US v1.05, ULUS-10041). They are copied into "
                + "the app's private storage.");
        body.setPadding(0, pad, 0, pad);
        root.addView(body);

        Button pick = new Button(this);
        pick.setText("Select game files");
        pick.setOnClickListener(v -> pickGameFolder());
        root.addView(pick);

        TextView hint = new TextView(this);
        hint.setText("They can also be copied to\n" + gameDir().getAbsolutePath());
        hint.setPadding(0, pad, 0, 0);
        hint.setTextSize(11);
        root.addView(hint);

        setContentView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private void pickGameFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQ_PICK_TREE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQ_PICK_TREE) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Not fatal: the grant lasts for this session anyway.
        }
        Toast.makeText(this, "Copying game files...", Toast.LENGTH_SHORT).show();
        copyDirectory(tree, DocumentsContract.getTreeDocumentId(tree), gameDir());
        if (gameFilesReady()) {
            super.onCreate(null);
        } else {
            Toast.makeText(this, "EBOOT.ELF / PSP_GAME not found in that folder",
                    Toast.LENGTH_LONG).show();
        }
    }

    /** Recursively copies a SAF document tree into internal storage. */
    private void copyDirectory(Uri tree, String documentId, File destination) {
        destination.mkdirs();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        try (Cursor cursor = getContentResolver().query(children, null, null, null, null)) {
            if (cursor == null) {
                return;
            }
            while (cursor.moveToNext()) {
                String childId = cursor.getString(cursor.getColumnIndexOrThrow(
                        DocumentsContract.Column.DOCUMENT_ID));
                String name = cursor.getString(cursor.getColumnIndexOrThrow(
                        DocumentsContract.Column.DOCUMENT_DISPLAY_NAME));
                Uri childUri = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                File target = new File(destination, name);
                if (cursor.isDirectory()) {
                    copyDirectory(tree, childId, target);
                } else {
                    copyFile(childUri, target);
                }
            }
        } catch (Exception ex) {
            Log.w(TAG, "copy failed for " + documentId + ": " + ex.getMessage());
        }
    }

    private void copyFile(Uri source, File target) {
        try (InputStream in = getContentResolver().openInputStream(source);
             OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        } catch (IOException ex) {
            Log.w(TAG, "copy failed: " + target.getName() + ": " + ex.getMessage());
        }
    }
}