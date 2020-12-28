// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import com.metallic.chiaki.lib.DiscoveryHost

sealed class DisplayHost
{
	abstract val registeredHost: RegisteredHost?
	abstract val host: String
	abstract val name: String?
	abstract val id: String?

	val isRegistered get() = registeredHost != null
}

class DiscoveredDisplayHost(
	override val registeredHost: RegisteredHost?,
	val discoveredHost: DiscoveryHost
): DisplayHost()
{
	override val host get() = discoveredHost.hostAddr ?: ""
	override val name get() = discoveredHost.hostName ?: registeredHost?.serverNickname
	override val id get() = discoveredHost.hostId ?: registeredHost?.serverMac?.toString()

	override fun equals(other: Any?): Boolean =
		if(other !is DiscoveredDisplayHost)
			false
		else
			other.discoveredHost == discoveredHost && other.registeredHost == registeredHost

	override fun hashCode() = 31 * (registeredHost?.hashCode() ?: 0) + discoveredHost.hashCode()

	override fun toString() = "DiscoveredDisplayHost{${registeredHost}, ${discoveredHost}}"
}

class ManualDisplayHost(
	override val registeredHost: RegisteredHost?,
	val manualHost: ManualHost
): DisplayHost()
{
	override val host get() = manualHost.host
	override val name get() = registeredHost?.serverNickname
	override val id get() = registeredHost?.serverMac?.toString()

	override fun equals(other: Any?): Boolean =
		if(other !is ManualDisplayHost)
			false
		else
			other.manualHost == manualHost && other.registeredHost == registeredHost

	override fun hashCode() = 31 * (registeredHost?.hashCode() ?: 0) + manualHost.hashCode()

	override fun toString() = "ManualDisplayHost{${registeredHost}, ${manualHost}}"
}