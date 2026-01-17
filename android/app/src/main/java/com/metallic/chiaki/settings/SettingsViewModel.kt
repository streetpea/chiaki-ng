// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import androidx.lifecycle.ViewModel
import com.metallic.chiaki.common.AppDatabase
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.common.ext.toLiveData

class SettingsViewModel(val database: AppDatabase, val preferences: Preferences): ViewModel()
{
	val registeredHostsCount = database.registeredHostDao().count().toLiveData()

	val bitrateAuto = preferences.bitrateAutoObservable.toLiveData()
}