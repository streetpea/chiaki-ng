// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.LiveData
import androidx.lifecycle.Observer
import com.metallic.chiaki.R
import com.metallic.chiaki.lib.ControllerState
import kotlinx.android.synthetic.main.fragment_controls.*

class TouchControlsFragment : Fragment()
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
	var onScreenControlsEnabled: LiveData<Boolean>? = null

	override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View
		= inflater.inflate(R.layout.fragment_controls, container, false)

	override fun onViewCreated(view: View, savedInstanceState: Bundle?)
	{
		super.onViewCreated(view, savedInstanceState)
		dpadView.stateChangeCallback = this::dpadStateChanged
		crossButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_CROSS)
		moonButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_MOON)
		pyramidButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_PYRAMID)
		boxButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_BOX)
		l1ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_L1)
		r1ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_R1)
		l3ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_L3)
		r3ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_R3)
		optionsButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_OPTIONS)
		shareButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_SHARE)
		psButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_PS)
		touchpadButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_TOUCHPAD)

		l2ButtonView.buttonPressedCallback = { controllerState = controllerState.copy().apply { l2State = if(it) 255U else 0U } }
		r2ButtonView.buttonPressedCallback = { controllerState = controllerState.copy().apply { r2State = if(it) 255U else 0U } }

		val quantizeStick = { f: Float ->
			(Short.MAX_VALUE * f).toShort()
		}

		leftAnalogStickView.stateChangedCallback = { controllerState = controllerState.copy().apply {
			leftX = quantizeStick(it.x)
			leftY = quantizeStick(it.y)
		}}

		rightAnalogStickView.stateChangedCallback = { controllerState = controllerState.copy().apply {
			rightX = quantizeStick(it.x)
			rightY = quantizeStick(it.y)
		}}

		onScreenControlsEnabled?.observe(this, Observer {
			view.visibility = if(it) View.VISIBLE else View.GONE
		})
	}

	private fun dpadStateChanged(direction: DPadView.Direction?)
	{
		controllerState = controllerState.copy().apply {
			buttons = ((buttons
						and ControllerState.BUTTON_DPAD_LEFT.inv()
						and ControllerState.BUTTON_DPAD_RIGHT.inv()
						and ControllerState.BUTTON_DPAD_UP.inv()
						and ControllerState.BUTTON_DPAD_DOWN.inv())
					or when(direction)
					{
						DPadView.Direction.UP -> ControllerState.BUTTON_DPAD_UP
						DPadView.Direction.DOWN -> ControllerState.BUTTON_DPAD_DOWN
						DPadView.Direction.LEFT -> ControllerState.BUTTON_DPAD_LEFT
						DPadView.Direction.RIGHT -> ControllerState.BUTTON_DPAD_RIGHT
						DPadView.Direction.LEFT_UP -> ControllerState.BUTTON_DPAD_LEFT or ControllerState.BUTTON_DPAD_UP
						DPadView.Direction.LEFT_DOWN -> ControllerState.BUTTON_DPAD_LEFT or ControllerState.BUTTON_DPAD_DOWN
						DPadView.Direction.RIGHT_UP -> ControllerState.BUTTON_DPAD_RIGHT or ControllerState.BUTTON_DPAD_UP
						DPadView.Direction.RIGHT_DOWN -> ControllerState.BUTTON_DPAD_RIGHT or ControllerState.BUTTON_DPAD_DOWN
						null -> 0U
					})
		}
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