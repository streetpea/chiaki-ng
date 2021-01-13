// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.view.View
import kotlin.math.sqrt

data class Vector(val x: Float, val y: Float)
{
	operator fun plus(o: Vector) = Vector(x + o.x, y + o.y)
	operator fun minus(o: Vector) = Vector(x - o.x, y - o.y)
	operator fun plus(s: Float) = Vector(x + s, y + s)
	operator fun minus(s: Float) = Vector(x - s, y - s)
	operator fun times(s: Float) = Vector(x * s, y * s)
	operator fun div(s: Float) = this * (1f / s)
	operator fun times(o: Vector) = Vector(x * o.x, y * o.y)
	operator fun div(o: Vector) = this * Vector(1.0f / o.x, 1.0f / o.y)

	val lengthSq get() = x*x + y*y
	val length get() = sqrt(lengthSq)
	val normalized get() = this / length
}

val View.locationOnScreen: Vector get() {
	val v = intArrayOf(0, 0)
	this.getLocationOnScreen(v)
	return Vector(v[0].toFloat(), v[1].toFloat())
}