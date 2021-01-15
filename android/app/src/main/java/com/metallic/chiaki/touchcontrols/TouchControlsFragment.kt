// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.LiveData
import androidx.lifecycle.Observer
import com.metallic.chiaki.databinding.FragmentControlsBinding
import com.metallic.chiaki.lib.ControllerState
import io.reactivex.Observable
import io.reactivex.Observable.combineLatest
import io.reactivex.subjects.BehaviorSubject
import io.reactivex.subjects.Subject

class TouchControlsFragment : Fragment()
{
	private var ownControllerState = ControllerState()
		private set(value)
		{
			val diff = field != value
			field = value
			if(diff)
				ownControllerStateSubject.onNext(ownControllerState)
		}

	private val ownControllerStateSubject: Subject<ControllerState>
			= BehaviorSubject.create<ControllerState>().also { it.onNext(ownControllerState) }

	// to delay attaching to the touchpadView until it's available
	private val controllerStateProxy: Subject<Observable<ControllerState>>
		= BehaviorSubject.create<Observable<ControllerState>>().also { it.onNext(ownControllerStateSubject) }
	val controllerState: Observable<ControllerState> get() =
		controllerStateProxy.flatMap { it }

	var onScreenControlsEnabled: LiveData<Boolean>? = null

	private var _binding: FragmentControlsBinding? = null
	private val binding get() = _binding!!

	override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View =
		FragmentControlsBinding.inflate(inflater, container, false).let {
			_binding = it
			controllerStateProxy.onNext(
				combineLatest(ownControllerStateSubject, binding.touchpadView.controllerState) { a, b -> a or b }
			)
			it.root
		}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?)
	{
		super.onViewCreated(view, savedInstanceState)
		binding.dpadView.stateChangeCallback = this::dpadStateChanged
		binding.crossButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_CROSS)
		binding.moonButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_MOON)
		binding.pyramidButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_PYRAMID)
		binding.boxButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_BOX)
		binding.l1ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_L1)
		binding.r1ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_R1)
		binding.l3ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_L3)
		binding.r3ButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_R3)
		binding.optionsButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_OPTIONS)
		binding.shareButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_SHARE)
		binding.psButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_PS)
		binding.touchpadButtonView.buttonPressedCallback = buttonStateChanged(ControllerState.BUTTON_TOUCHPAD)

		binding.l2ButtonView.buttonPressedCallback = { ownControllerState = ownControllerState.copy().apply { l2State = if(it) 255U else 0U } }
		binding.r2ButtonView.buttonPressedCallback = { ownControllerState = ownControllerState.copy().apply { r2State = if(it) 255U else 0U } }

		val quantizeStick = { f: Float ->
			(Short.MAX_VALUE * f).toInt().toShort()
		}

		binding.leftAnalogStickView.stateChangedCallback = { ownControllerState = ownControllerState.copy().apply {
			leftX = quantizeStick(it.x)
			leftY = quantizeStick(it.y)
		}}

		binding.rightAnalogStickView.stateChangedCallback = { ownControllerState = ownControllerState.copy().apply {
			rightX = quantizeStick(it.x)
			rightY = quantizeStick(it.y)
		}}

		onScreenControlsEnabled?.observe(viewLifecycleOwner, Observer {
			view.visibility = if(it) View.VISIBLE else View.GONE
		})
	}

	private fun dpadStateChanged(direction: DPadView.Direction?)
	{
		ownControllerState = ownControllerState.copy().apply {
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
		ownControllerState = ownControllerState.copy().apply {
			buttons =
				if(pressed)
					buttons or buttonMask
				else
					buttons and buttonMask.inv()

		}
	}
}