package com.metallic.chiaki.touchcontrols

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import androidx.appcompat.app.AppCompatActivity
import com.metallic.chiaki.common.Preferences

class ButtonHaptics(val context: Context)
{
	private val enabled = Preferences(context).buttonHapticEnabled

	fun trigger(harder: Boolean = false)
	{
		if(!enabled)
			return
		val vibrator = context.getSystemService(AppCompatActivity.VIBRATOR_SERVICE) as Vibrator
		if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
			vibrator.vibrate(
				if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
					VibrationEffect.createPredefined(if(harder) VibrationEffect.EFFECT_CLICK else VibrationEffect.EFFECT_TICK)
				else
					VibrationEffect.createOneShot(10, if(harder) 200 else 100)
			)
		else
			vibrator.vibrate(10)
	}
}