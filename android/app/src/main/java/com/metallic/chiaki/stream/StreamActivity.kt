// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.app.AlertDialog
import android.content.res.Configuration
import android.graphics.Matrix
import android.os.Bundle
import android.os.Handler
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.TextureView
import android.view.View
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.isGone
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.lifecycle.*
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.metallic.chiaki.R
import com.metallic.chiaki.common.LogManager
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.common.ext.viewModelFactory
import com.metallic.chiaki.lib.ConnectInfo
import com.metallic.chiaki.lib.ConnectVideoProfile
import com.metallic.chiaki.session.*
import com.metallic.chiaki.touchcontrols.TouchpadOnlyFragment
import com.metallic.chiaki.touchcontrols.TouchControlsFragment
import kotlinx.android.synthetic.main.activity_stream.*
import kotlin.math.min

private sealed class DialogContents
private object StreamQuitDialog: DialogContents()
private object CreateErrorDialog: DialogContents()
private object PinRequestDialog: DialogContents()

class StreamActivity : AppCompatActivity(), View.OnSystemUiVisibilityChangeListener
{
	companion object
	{
		const val EXTRA_CONNECT_INFO = "connect_info"
		private const val HIDE_UI_TIMEOUT_MS = 2000L
	}

	private lateinit var viewModel: StreamViewModel
	private val uiVisibilityHandler = Handler()

	override fun onCreate(savedInstanceState: Bundle?)
	{
		super.onCreate(savedInstanceState)

		val connectInfo = intent.getParcelableExtra<ConnectInfo>(EXTRA_CONNECT_INFO)
		if(connectInfo == null)
		{
			finish()
			return
		}

		viewModel = ViewModelProvider(this, viewModelFactory {
			StreamViewModel(application, connectInfo)
		})[StreamViewModel::class.java]

		viewModel.input.observe(this)

		setContentView(R.layout.activity_stream)
		window.decorView.setOnSystemUiVisibilityChangeListener(this)

		viewModel.onScreenControlsEnabled.observe(this, Observer {
			if(onScreenControlsSwitch.isChecked != it)
				onScreenControlsSwitch.isChecked = it
			if(onScreenControlsSwitch.isChecked)
				touchpadOnlySwitch.isChecked = false
		})
		onScreenControlsSwitch.setOnCheckedChangeListener { _, isChecked ->
			viewModel.setOnScreenControlsEnabled(isChecked)
			showOverlay()
		}

		viewModel.touchpadOnlyEnabled.observe(this, Observer {
			if(touchpadOnlySwitch.isChecked != it)
				touchpadOnlySwitch.isChecked = it
			if(touchpadOnlySwitch.isChecked)
				onScreenControlsSwitch.isChecked = false
		})
		touchpadOnlySwitch.setOnCheckedChangeListener { _, isChecked ->
			viewModel.setTouchpadOnlyEnabled(isChecked)
			showOverlay()
		}

		displayModeToggle.addOnButtonCheckedListener { _, checkedId, _ ->
			// following 'if' is a workaround until selectionRequired for MaterialButtonToggleGroup
			// comes out of alpha.
			// See https://stackoverflow.com/questions/56164004/required-single-selection-on-materialbuttontogglegroup
			if (displayModeToggle.checkedButtonId == -1)
				displayModeToggle.check(checkedId)

			adjustTextureViewAspect()
			showOverlay()
		}

		viewModel.session.attachToTextureView(textureView)
		viewModel.session.state.observe(this, Observer { this.stateChanged(it) })
		textureView.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
			adjustTextureViewAspect()
		}

		if(Preferences(this).rumbleEnabled)
		{
			val vibrator = getSystemService(VIBRATOR_SERVICE) as Vibrator
			viewModel.session.rumbleState.observe(this, Observer {
				val amplitude = min(255, (it.left.toInt() + it.right.toInt()) / 2)
				vibrator.cancel()
				if(amplitude == 0)
					return@Observer
				if(android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O)
					vibrator.vibrate(VibrationEffect.createOneShot(1000, amplitude))
			})
		}
	}

	override fun onAttachFragment(fragment: Fragment)
	{
		super.onAttachFragment(fragment)
		if(fragment is TouchControlsFragment)
		{
			fragment.controllerStateCallback = { viewModel.input.touchControllerState = it }
			fragment.onScreenControlsEnabled = viewModel.onScreenControlsEnabled
		}
		if(fragment is TouchpadOnlyFragment)
		{
			fragment.controllerStateCallback = { viewModel.input.touchControllerState = it }
			fragment.touchpadOnlyEnabled = viewModel.touchpadOnlyEnabled
		}
	}

	override fun onResume()
	{
		super.onResume()
		hideSystemUI()
		viewModel.session.resume()
	}

	override fun onPause()
	{
		super.onPause()
		viewModel.session.pause()
	}

	private fun reconnect()
	{
		viewModel.session.shutdown()
		viewModel.session.resume()
	}

	private val hideSystemUIRunnable = Runnable { hideSystemUI() }

	override fun onSystemUiVisibilityChange(visibility: Int)
	{
		if(visibility and View.SYSTEM_UI_FLAG_FULLSCREEN == 0)
			showOverlay()
		else
			hideOverlay()
	}

	private fun showOverlay()
	{
		overlay.isVisible = true
		overlay.animate()
			.alpha(1.0f)
			.setListener(object: AnimatorListenerAdapter()
			{
				override fun onAnimationEnd(animation: Animator?)
				{
					overlay.alpha = 1.0f
				}
			})
		uiVisibilityHandler.removeCallbacks(hideSystemUIRunnable)
		uiVisibilityHandler.postDelayed(hideSystemUIRunnable, HIDE_UI_TIMEOUT_MS)
	}

	private fun hideOverlay()
	{
		overlay.animate()
			.alpha(0.0f)
			.setListener(object: AnimatorListenerAdapter()
			{
				override fun onAnimationEnd(animation: Animator?)
				{
					overlay.isGone = true
				}
			})
	}

	override fun onWindowFocusChanged(hasFocus: Boolean)
	{
		super.onWindowFocusChanged(hasFocus)
		if(hasFocus)
			hideSystemUI()
	}

	private fun hideSystemUI()
	{
		window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE
				or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
				or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
				or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
				or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
				or View.SYSTEM_UI_FLAG_FULLSCREEN)
	}

	private var dialogContents: DialogContents? = null
	private var dialog: AlertDialog? = null
		set(value)
		{
			field = value
			if(value == null)
				dialogContents = null
		}

	private fun stateChanged(state: StreamState)
	{
		progressBar.visibility = if(state == StreamStateConnecting) View.VISIBLE else View.GONE

		when(state)
		{
			is StreamStateQuit ->
			{
				if(!state.reason.isStopped && dialogContents != StreamQuitDialog)
				{
					dialog?.dismiss()
					val reasonStr = state.reasonString
					val dialog = MaterialAlertDialogBuilder(this)
						.setMessage(getString(R.string.alert_message_session_quit, state.reason.toString())
								+ (if(reasonStr != null) "\n$reasonStr" else ""))
						.setPositiveButton(R.string.action_reconnect) { _, _ ->
							dialog = null
							reconnect()
						}
						.setOnCancelListener {
							dialog = null
							finish()
						}
						.setNegativeButton(R.string.action_quit_session) { _, _ ->
							dialog = null
							finish()
						}
						.create()
					dialogContents = StreamQuitDialog
					dialog.show()
				}
			}

			is StreamStateCreateError ->
			{
				if(dialogContents != CreateErrorDialog)
				{
					dialog?.dismiss()
					val dialog = MaterialAlertDialogBuilder(this)
						.setMessage(getString(R.string.alert_message_session_create_error, state.error.errorCode.toString()))
						.setOnDismissListener {
							dialog = null
							finish()
						}
						.setNegativeButton(R.string.action_quit_session) { _, _ -> }
						.create()
					dialogContents = CreateErrorDialog
					dialog.show()
				}
			}

			is StreamStateLoginPinRequest ->
			{
				if(dialogContents != PinRequestDialog)
				{
					dialog?.dismiss()

					val view = layoutInflater.inflate(R.layout.dialog_login_pin, null)
					val pinEditText = view.findViewById<EditText>(R.id.pinEditText)

					val dialog = MaterialAlertDialogBuilder(this)
						.setMessage(
							if(state.pinIncorrect)
								R.string.alert_message_login_pin_request_incorrect
							else
								R.string.alert_message_login_pin_request)
						.setView(view)
						.setPositiveButton(R.string.action_login_pin_connect) { _, _ ->
							dialog = null
							viewModel.session.setLoginPin(pinEditText.text.toString())
						}
						.setOnCancelListener {
							dialog = null
							finish()
						}
						.setNegativeButton(R.string.action_quit_session) { _, _ ->
							dialog = null
							finish()
						}
						.create()
					dialogContents = PinRequestDialog
					dialog.show()
				}
			}
		}
	}

	private fun adjustTextureViewAspect()
	{
		val displayInfo = DisplayInfo(viewModel.session.connectInfo.videoProfile, textureView)
		val resolution = displayInfo.computeResolutionFor(displayModeToggle.checkedButtonId)

		Matrix().also {
			textureView.getTransform(it)
			it.setScale(resolution.width / displayInfo.viewWidth, resolution.height / displayInfo.viewHeight)
			it.postTranslate((displayInfo.viewWidth - resolution.width) * 0.5f, (displayInfo.viewHeight - resolution.height) * 0.5f)
			textureView.setTransform(it)
		}
	}

	override fun dispatchKeyEvent(event: KeyEvent) = viewModel.input.dispatchKeyEvent(event) || super.dispatchKeyEvent(event)
	override fun onGenericMotionEvent(event: MotionEvent) = viewModel.input.onGenericMotionEvent(event) || super.onGenericMotionEvent(event)
}


class DisplayInfo constructor(val videoProfile: ConnectVideoProfile, val textureView: TextureView)
{
	val contentWidth : Float get() = videoProfile.width.toFloat()
	val contentHeight : Float get() = videoProfile.height.toFloat()
	val viewWidth : Float get() = textureView.width.toFloat()
	val viewHeight : Float get() = textureView.height.toFloat()
	val contentAspect : Float get() =  contentHeight / contentWidth

	fun computeResolutionFor(displayModeButtonId: Int) : Resolution
	{
		when (displayModeButtonId)
		{
			R.id.display_mode_stretch_button -> return computeStrechedResolution()
			R.id.display_mode_zoom_button -> return computeZoomedResolution()
			else -> return computeNormalResolution()
		}
	}

	private fun computeStrechedResolution(): Resolution
	{
		return Resolution(viewWidth, viewHeight)
	}

	private fun computeZoomedResolution(): Resolution
	{
		if (viewHeight > viewWidth * contentAspect)
		{
			val zoomFactor = viewHeight / contentHeight
			return Resolution(contentWidth * zoomFactor, viewHeight)
		}
		else
		{
			val zoomFactor = viewWidth / contentWidth
			return Resolution(viewWidth, contentHeight * zoomFactor)
		}
	}

	private fun computeNormalResolution(): Resolution
	{
		if (viewHeight > viewWidth * contentAspect)
		{
			return Resolution(viewWidth, viewWidth * contentAspect)
		}
		else
		{
			return Resolution(viewHeight / contentAspect, viewHeight)
		}
	}

}


data class Resolution(val width: Float, val height: Float)
