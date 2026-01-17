// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import android.content.Context
import android.util.AttributeSet
import android.view.KeyEvent
import androidx.appcompat.app.AlertDialog
import androidx.preference.Preference
import com.metallic.chiaki.R

class KeyMappingPreference(context: Context, attrs: AttributeSet?) : Preference(context, attrs) {

    override fun onClick() {
        showMappingDialog()
    }

    private fun showMappingDialog() {
        val dialog = AlertDialog.Builder(context)
            .setTitle(title)
            .setMessage("Press a button on your controller...")
            .setNegativeButton(android.R.string.cancel, null)
            .setNeutralButton("Clear") { _, _ ->
                persistString("0")
                summary = getFriendlyKeyName(0)
            }
            .create()

        dialog.setOnKeyListener { d, keyCode, event ->
            if (event.action == KeyEvent.ACTION_DOWN) {
                // Ignore some system keys if necessary, but generally we want to allow any button
                if (keyCode != KeyEvent.KEYCODE_BACK && keyCode != KeyEvent.KEYCODE_ESCAPE) {
                    persistString(keyCode.toString())
                    summary = getFriendlyKeyName(keyCode)
                    d.dismiss()
                    true
                } else false
            } else false
        }

        dialog.show()
    }

    override fun onSetInitialValue(defaultValue: Any?) {
        super.onSetInitialValue(defaultValue)
        val value = getPersistedString(defaultValue as? String ?: "0")
        summary = getFriendlyKeyName(value.toIntOrNull() ?: 0)
    }

    private fun getFriendlyKeyName(keyCode: Int): String {
        if (keyCode == 0) return "Not set"
        return KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_")
    }
}
