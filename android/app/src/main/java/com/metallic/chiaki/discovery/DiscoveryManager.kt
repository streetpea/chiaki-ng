// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.discovery

import android.util.Log
import com.metallic.chiaki.common.MacAddress
import com.metallic.chiaki.common.ext.hexToByteArray
import com.metallic.chiaki.lib.CreateError
import com.metallic.chiaki.lib.DiscoveryHost
import com.metallic.chiaki.lib.DiscoveryService
import com.metallic.chiaki.lib.DiscoveryServiceOptions
import io.reactivex.Observable
import io.reactivex.android.schedulers.AndroidSchedulers
import io.reactivex.disposables.CompositeDisposable
import io.reactivex.rxkotlin.addTo
import io.reactivex.subjects.BehaviorSubject
import io.reactivex.subjects.Subject
import java.lang.NumberFormatException
import java.net.InetSocketAddress
import java.nio.charset.Charset
import java.nio.charset.StandardCharsets
import java.util.concurrent.TimeUnit

val DiscoveryHost.serverMac get() = this.hostId?.hexToByteArray()?.let {
	if(it.size == MacAddress.LENGTH)
		MacAddress(it)
	else
		null
}

class DiscoveryManager
{
	companion object
	{
		const val HOSTS_MAX: ULong = 16U
		const val DROP_PINGS: ULong = 3U
		const val PING_MS: ULong = 500U
		const val PORT = 987

		const val DEBOUNCE_EMPTY_MS = 1000L
	}

	private var discoveryService: DiscoveryService? = null

	private val discoveryActiveSubject: Subject<Boolean> = BehaviorSubject.create<Boolean>().also { it.onNext(false) }
	val discoveryActive: Observable<Boolean> get() = discoveryActiveSubject
	var active = false
		set(value)
		{
			field = value
			discoveryActiveSubject.onNext(value)
			updateService()
		}
	private var paused = false

	private val disposable = CompositeDisposable()

	private var discoveredHostsSubjectDebounced: Subject<List<DiscoveryHost>> = BehaviorSubject.create<List<DiscoveryHost>>().also {
		it.onNext(listOf())
	}.toSerialized()

	private var discoveredHostsSubjectRaw: Subject<List<DiscoveryHost>> = BehaviorSubject.create<List<DiscoveryHost>>().also { subject ->
		subject.debounce { hosts ->
				if(hosts.isEmpty())
					Observable.timer(DEBOUNCE_EMPTY_MS, TimeUnit.MILLISECONDS)
				else
					Observable.empty()
			}
			.subscribe { hosts ->
				discoveredHostsSubjectDebounced.onNext(hosts)
			}
			.addTo(disposable)
	}

	val discoveredHosts: Observable<List<DiscoveryHost>> get() = discoveredHostsSubjectDebounced

	fun resume()
	{
		paused = false
		updateService()
	}

	fun pause()
	{
		paused = true
		updateService()
	}

	fun dispose()
	{
		active = false
		disposable.dispose()
	}

	fun sendWakeup(host: String, registKey: ByteArray, ps5: Boolean)
	{
		val registKeyString = registKey.indexOfFirst { it == 0.toByte() }.let { end -> registKey.copyOfRange(0, if(end >= 0) end else registKey.size) }.toString(StandardCharsets.UTF_8)
		val credential = try { registKeyString.toULong(16) } catch(e: NumberFormatException) {
			Log.e("DiscoveryManager", "Failed to convert registKey to int", e)
			return
		}
		DiscoveryService.wakeup(discoveryService, host, credential, ps5)
	}

	private fun updateService()
	{
		if(active && !paused && discoveryService == null)
		{
			discoveredHostsSubjectRaw.onNext(listOf())
			try
			{
				discoveryService = DiscoveryService(DiscoveryServiceOptions(
					HOSTS_MAX, DROP_PINGS, PING_MS, InetSocketAddress("255.255.255.255", PORT)
				), discoveredHostsSubjectRaw::onNext)
			}
			catch(e: CreateError)
			{
				Log.e("DiscoveryManager", "Failed to start Discovery Service: $e")
			}
		}
		else if((!active || paused) && discoveryService != null)
		{
			val service = discoveryService ?: return
			service.dispose()
			discoveryService = null
			if(!active)
				discoveredHostsSubjectRaw.onNext(listOf())
		}
	}
}