// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.content.Context
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import com.metallic.chiaki.R
import com.metallic.chiaki.lib.ControllerState

class TouchpadView @JvmOverloads constructor(
	context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr)
{
	val state: ControllerState = ControllerState()
	private val pointerTouchIds = mutableMapOf<Int, UByte>()

	var stateChangeCallback: ((ControllerState) -> Unit)? = null

	private val drawable: Drawable?

	init
	{
		context.theme.obtainStyledAttributes(attrs, R.styleable.TouchpadView, 0, 0).apply {
			drawable = getDrawable(R.styleable.TouchpadView_drawable)
			recycle()
		}
		isClickable = true
	}

	override fun onDraw(canvas: Canvas)
	{
		super.onDraw(canvas)
		if(state.touches.find { it.id >= 0 } == null)
			return
		drawable?.setBounds(paddingLeft, paddingTop, width - paddingRight, height - paddingBottom)
		drawable?.draw(canvas)
	}

	private fun touchX(event: MotionEvent, index: Int): UShort =
		maxOf(0U.toUShort(), minOf((ControllerState.TOUCHPAD_WIDTH - 1u).toUShort(),
			(ControllerState.TOUCHPAD_WIDTH.toFloat() * event.getX(index) / width.toFloat()).toUInt().toUShort()))

	private fun touchY(event: MotionEvent, index: Int): UShort =
		maxOf(0U.toUShort(), minOf((ControllerState.TOUCHPAD_HEIGHT - 1u).toUShort(),
			(ControllerState.TOUCHPAD_HEIGHT.toFloat() * event.getY(index) / height.toFloat()).toUInt().toUShort()))

	override fun onTouchEvent(event: MotionEvent): Boolean
	{
		when(event.actionMasked)
		{
			MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
				state.startTouch(touchX(event, event.actionIndex), touchY(event, event.actionIndex))?.let {
					pointerTouchIds[event.getPointerId(event.actionIndex)] = it
					triggerStateChanged()
				}
			}
			MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
				pointerTouchIds.remove(event.getPointerId(event.actionIndex))?.let {
					state.stopTouch(it)
					triggerStateChanged()
				}
			}
			MotionEvent.ACTION_MOVE -> {
				val changed = pointerTouchIds.entries.fold(false) { acc, it ->
					val index = event.findPointerIndex(it.key)
					if(index < 0)
						acc
					else
						acc || state.setTouchPos(it.value, touchX(event, index), touchY(event, index))
				}
				if(changed)
					triggerStateChanged()
			}
		}
		return true
	}

	private fun triggerStateChanged()
	{
		stateChangeCallback?.let { it(state) }
	}
}