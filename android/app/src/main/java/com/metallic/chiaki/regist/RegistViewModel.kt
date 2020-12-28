// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.regist

import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel

class RegistViewModel: ViewModel()
{
	enum class ConsoleVersion {
		PS5,
		PS4_GE_8,
		PS4_GE_7,
		PS4_LT_7;

		val isPS5 get() = this == PS5
	}

	val ps4Version = MutableLiveData<ConsoleVersion>(ConsoleVersion.PS5)
}