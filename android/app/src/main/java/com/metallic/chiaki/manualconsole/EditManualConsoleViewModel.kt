// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.manualconsole

import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import com.metallic.chiaki.common.AppDatabase
import com.metallic.chiaki.common.ManualHost
import com.metallic.chiaki.common.RegisteredHost
import com.metallic.chiaki.common.ext.toLiveData
import io.reactivex.android.schedulers.AndroidSchedulers
import io.reactivex.schedulers.Schedulers

class EditManualConsoleViewModel(val database: AppDatabase, manualHostId: Long?): ViewModel()
{
	val registeredHosts = database.registeredHostDao().getAll().observeOn(AndroidSchedulers.mainThread())
			.doOnNext { hosts ->
				val selectedHost = selectedRegisteredHost.value
				if(selectedHost != null)
					selectedRegisteredHost.value = hosts.firstOrNull { it.id == selectedHost.id }
			}
			.map { listOf(null) + it }
			.toLiveData()

	val existingHost: LiveData<ManualHost>? =
		if(manualHostId != null)
			database.manualHostDao()
				.getByIdWithRegisteredHost(manualHostId)
				.toFlowable()
				.doOnError {
					Log.e("EditManualConsole", "Failed to fetch existing manual host", it)
				}
				.subscribeOn(Schedulers.io())
				.observeOn(AndroidSchedulers.mainThread())
				.doOnNext { hosts ->
					selectedRegisteredHost.value = hosts.registeredHost
				}
				.map { hosts -> hosts.manualHost }
				.toLiveData()
		else
			null

	var selectedRegisteredHost = MutableLiveData<RegisteredHost?>(null)

	fun saveHost(host: String) =
		database.manualHostDao()
			.let {
				val registeredHost = selectedRegisteredHost.value?.id
				val existingHost = existingHost?.value
				if(existingHost != null)
					it.update(ManualHost(id = existingHost.id, host = host, registeredHost = registeredHost))
				else
					it.insert(ManualHost(host = host, registeredHost = registeredHost))
			}
			.subscribeOn(Schedulers.io())
}