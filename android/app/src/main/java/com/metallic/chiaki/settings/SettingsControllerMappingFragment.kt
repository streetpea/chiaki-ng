// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import android.content.res.Resources
import android.os.Bundle
import android.view.KeyEvent
import androidx.appcompat.app.AlertDialog
import androidx.lifecycle.ViewModelProvider
import androidx.preference.*
import com.metallic.chiaki.R
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.common.ext.viewModelFactory
import com.metallic.chiaki.common.getDatabase

class SettingsControllerMappingFragment : PreferenceFragmentCompat(), TitleFragment
{
	private var currentMappingPreference: Preference? = null
	private var mappingDialog: AlertDialog? = null

	override fun getTitle(resources: Resources) = resources.getString(R.string.preferences_controller_mapping_title)

	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?)
	{
		val context = context ?: return
		val viewModel = ViewModelProvider(this, viewModelFactory { SettingsViewModel(getDatabase(context), Preferences(context)) })
			.get(SettingsViewModel::class.java)

		setPreferencesFromResource(R.xml.preferences_controller_mapping, rootKey)

		// Setup button mapping preferences
		setupButtonMapping("cross", R.string.preferences_button_cross_title, viewModel.preferences)
		setupButtonMapping("moon", R.string.preferences_button_moon_title, viewModel.preferences)
		setupButtonMapping("box", R.string.preferences_button_box_title, viewModel.preferences)
		setupButtonMapping("pyramid", R.string.preferences_button_pyramid_title, viewModel.preferences)
		setupButtonMapping("l1", R.string.preferences_button_l1_title, viewModel.preferences)
		setupButtonMapping("r1", R.string.preferences_button_r1_title, viewModel.preferences)
		setupButtonMapping("l2", R.string.preferences_button_l2_title, viewModel.preferences)
		setupButtonMapping("r2", R.string.preferences_button_r2_title, viewModel.preferences)
		setupButtonMapping("l3", R.string.preferences_button_l3_title, viewModel.preferences)
		setupButtonMapping("r3", R.string.preferences_button_r3_title, viewModel.preferences)
		setupButtonMapping("options", R.string.preferences_button_options_title, viewModel.preferences)
		setupButtonMapping("share", R.string.preferences_button_share_title, viewModel.preferences)
		setupButtonMapping("ps", R.string.preferences_button_ps_title, viewModel.preferences)
		setupButtonMapping("touchpad", R.string.preferences_button_touchpad_title, viewModel.preferences)

		// Reset to defaults preference
		preferenceScreen.findPreference<Preference>("reset_mappings")?.setOnPreferenceClickListener {
			resetMappingsToDefault(viewModel.preferences)
			true
		}
	}

	private fun setupButtonMapping(buttonKey: String, titleRes: Int, preferences: Preferences)
	{
		val prefKey = "button_mapping_$buttonKey"
		preferenceScreen.findPreference<Preference>(prefKey)?.apply {
			// Load current mapping
			val currentKeyCode = preferences.getButtonMapping(buttonKey)
			summary = getKeyCodeName(currentKeyCode)

			setOnPreferenceClickListener { pref ->
				currentMappingPreference = pref
				showMappingDialog(buttonKey, titleRes, preferences)
				true
			}
		}
	}

	private fun showMappingDialog(buttonKey: String, titleRes: Int, preferences: Preferences)
	{
		mappingDialog?.dismiss()
		
		val builder = AlertDialog.Builder(requireContext())
			.setTitle(getString(R.string.preferences_button_mapping_dialog_title, getString(titleRes)))
			.setMessage(R.string.preferences_button_mapping_dialog_message)
			.setNegativeButton(R.string.action_cancel) { dialog, _ ->
				dialog.dismiss()
				mappingDialog = null
			}
			.setNeutralButton(R.string.preferences_button_mapping_clear) { dialog, _ ->
				// Clear mapping
				preferences.clearButtonMapping(buttonKey)
				currentMappingPreference?.summary = getString(R.string.preferences_button_mapping_none)
				dialog.dismiss()
				mappingDialog = null
			}
			.setOnKeyListener { dialog, keyCode, event ->
				if (event.action == KeyEvent.ACTION_DOWN && keyCode != KeyEvent.KEYCODE_BACK)
				{
					// Save the mapping
					preferences.saveButtonMapping(buttonKey, keyCode)
					currentMappingPreference?.summary = getKeyCodeName(keyCode)
					dialog.dismiss()
					mappingDialog = null
					true
				}
				else
				{
					false
				}
			}

		mappingDialog = builder.create()
		mappingDialog?.show()
	}

	private fun getKeyCodeName(keyCode: Int): String
	{
		return when (keyCode)
		{
			KeyEvent.KEYCODE_BUTTON_A -> "A"
			KeyEvent.KEYCODE_BUTTON_B -> "B"
			KeyEvent.KEYCODE_BUTTON_X -> "X"
			KeyEvent.KEYCODE_BUTTON_Y -> "Y"
			KeyEvent.KEYCODE_BUTTON_L1 -> "L1"
			KeyEvent.KEYCODE_BUTTON_R1 -> "R1"
			KeyEvent.KEYCODE_BUTTON_L2 -> "L2"
			KeyEvent.KEYCODE_BUTTON_R2 -> "R2"
			KeyEvent.KEYCODE_BUTTON_THUMBL -> "L3"
			KeyEvent.KEYCODE_BUTTON_THUMBR -> "R3"
			KeyEvent.KEYCODE_BUTTON_SELECT -> "Select"
			KeyEvent.KEYCODE_BUTTON_START -> "Start"
			KeyEvent.KEYCODE_BUTTON_MODE -> "Mode"
			KeyEvent.KEYCODE_BUTTON_C -> "C"
			KeyEvent.KEYCODE_DPAD_UP -> "D-Pad Up"
			KeyEvent.KEYCODE_DPAD_DOWN -> "D-Pad Down"
			KeyEvent.KEYCODE_DPAD_LEFT -> "D-Pad Left"
			KeyEvent.KEYCODE_DPAD_RIGHT -> "D-Pad Right"
			-1 -> getString(R.string.preferences_button_mapping_none)
			else -> "Button $keyCode"
		}
	}

	private fun resetMappingsToDefault(preferences: Preferences)
	{
		AlertDialog.Builder(requireContext())
			.setTitle(R.string.preferences_reset_mappings_confirm_title)
			.setMessage(R.string.preferences_reset_mappings_confirm_message)
			.setPositiveButton(R.string.action_reset) { _, _ ->
				preferences.resetButtonMappingsToDefault()
				// Refresh all preference summaries
				preferenceScreen.preferenceCount.let { count ->
					for (i in 0 until count)
					{
						val category = preferenceScreen.getPreference(i) as? PreferenceCategory ?: continue
						for (j in 0 until category.preferenceCount)
						{
							val pref = category.getPreference(j)
							if (pref.key?.startsWith("button_mapping_") == true)
							{
								val buttonKey = pref.key.removePrefix("button_mapping_")
								pref.summary = getKeyCodeName(preferences.getButtonMapping(buttonKey))
							}
						}
					}
				}
			}
			.setNegativeButton(R.string.action_cancel, null)
			.show()
	}

	override fun onDestroyView()
	{
		mappingDialog?.dismiss()
		mappingDialog = null
		super.onDestroyView()
	}
}
