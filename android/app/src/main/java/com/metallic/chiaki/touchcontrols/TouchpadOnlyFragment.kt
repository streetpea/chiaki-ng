// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.LiveData
import androidx.lifecycle.Observer
import com.metallic.chiaki.R
import com.metallic.chiaki.databinding.FragmentControlsBinding
import com.metallic.chiaki.databinding.FragmentTouchpadOnlyBinding
import com.metallic.chiaki.lib.ControllerState

class TouchpadOnlyFragment : Fragment()
{
	private var controllerState = ControllerState()
		private set(value)
		{
			val diff = field != value
			field = value
			if(diff)
				controllerStateCallback?.let { it(value) }
		}

	var controllerStateCallback: ((ControllerState) -> Unit)? = null
	var touchpadOnlyEnabled: LiveData<Boolean>? = null

	private var _binding: FragmentTouchpadOnlyBinding? = null
	private val binding get() = _binding!!

	override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View =
		FragmentTouchpadOnlyBinding.inflate(inflater, container, false).let {
			_binding = it
			it.root
		}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?)
	{
		super.onViewCreated(view, savedInstanceState)

		binding.touchpadButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_TOUCHPAD)

		touchpadOnlyEnabled?.observe(viewLifecycleOwner, Observer {
			view.visibility = if(it) View.VISIBLE else View.GONE
		})
	}

	private fun buttonStateChanged(buttonMask: UInt) = { pressed: Boolean ->
		controllerState = controllerState.copy().apply {
			buttons =
				if(pressed)
					buttons or buttonMask
				else
					buttons and buttonMask.inv()

		}
	}
}