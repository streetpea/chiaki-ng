// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.stream

import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.util.Log
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.random.Random

/**
 * GLSurfaceView.Renderer that applies debanding shader to video frames.
 * Uses SurfaceTexture to receive frames from MediaCodec and renders them
 * with a GLSL debanding effect.
 */
class DebandRenderer(
    private val onSurfaceReady: (Surface) -> Unit
) : GLSurfaceView.Renderer, SurfaceTexture.OnFrameAvailableListener {

    companion object {
        private const val TAG = "DebandRenderer"

        // 1. Copy pass (OES -> FBO)
        private const val COPY_VERTEX_SHADER = """
            #version 300 es
            in vec4 a_Position;
            in vec2 a_TexCoord;
            out highp vec2 v_TexCoord;
            uniform mat4 u_STMatrix;
            void main() {
                gl_Position = a_Position;
                v_TexCoord = (u_STMatrix * vec4(a_TexCoord, 0.0, 1.0)).xy;
            }
        """

        private const val COPY_FRAGMENT_SHADER = """
            #version 300 es
            #extension GL_OES_EGL_image_external_essl3 : require
            precision highp float;
            in highp vec2 v_TexCoord;
            out vec4 outColor;
            uniform samplerExternalOES u_Texture;
            void main() {
                outColor = texture(u_Texture, v_TexCoord);
            }
        """

        // 2. Effects pass (FBO -> Screen)
        private const val EFFECT_VERTEX_SHADER = """
            #version 300 es
            in vec4 a_Position;
            in vec2 a_TexCoord;
            out highp vec2 v_TexCoord;
            void main() {
                gl_Position = a_Position;
                // FBO texture is usually upside down relative to OES coordinates
                v_TexCoord = a_TexCoord;
            }
        """

        private const val EFFECT_FRAGMENT_SHADER = """
            #version 300 es
            precision highp float;

            in highp vec2 v_TexCoord;
            out vec4 outColor;

            uniform sampler2D u_Texture;
            uniform highp float u_Time;
            uniform highp vec2 u_ScreenSize;
            uniform highp float u_Sharpness;

            const float DEBAND_THRESHOLD = 0.04;
            const float DEBAND_RANGE = 32.0;
            const float DITHER_GRAIN = 0.012;

            highp float ign(vec2 v) {
                v = floor(v * u_ScreenSize);
                return fract(52.9829189 * fract(dot(v, vec2(0.06711056, 0.00583715))));
            }

            highp float rand(vec2 co, float seed) {
                return fract(sin(dot(co + seed, vec2(12.9898, 78.233))) * 43758.5453);
            }

            void main() {
                highp vec2 texelSize = 1.0 / u_ScreenSize;
                float frameSeed = fract(u_Time * 0.05); 
                
                // 1. Debanding Pass
                // We must sample 4 specific neighbors for later RCAS usage to avoid re-sampling texture
                vec3 color = texture(u_Texture, v_TexCoord).rgb;
                vec3 b = texture(u_Texture, v_TexCoord + vec2(0.0, -texelSize.y)).rgb;
                vec3 d = texture(u_Texture, v_TexCoord + vec2(-texelSize.x, 0.0)).rgb;
                vec3 f = texture(u_Texture, v_TexCoord + vec2(texelSize.x, 0.0)).rgb;
                vec3 h = texture(u_Texture, v_TexCoord + vec2(0.0, texelSize.y)).rgb;

                // Simple deband on center pixel (simplified but works with FSR)
                for (int i = 0; i < 8; i++) {
                    float angle = float(i) * 0.785 + rand(v_TexCoord, 0.0) * 0.5;
                    float dist = DEBAND_RANGE * (0.3 + 0.7 * rand(v_TexCoord, float(i)));
                    highp vec2 dir = vec2(cos(angle), sin(angle));
                    vec3 sampleColor = texture(u_Texture, clamp(v_TexCoord + dir * dist * texelSize, 0.0, 1.0)).rgb;
                    float weight = 1.0 - smoothstep(0.0, DEBAND_THRESHOLD, max(max(abs(color.r - sampleColor.r), abs(color.g - sampleColor.g)), abs(color.b - sampleColor.b)));
                    color = mix(color, sampleColor, weight * 0.3);
                }

                // 2. FSR 1.0 (RCAS)
                // IMPORTANT: We must use the CLEANED color, but RCAS needs neighbors.
                // For mobile efficiency, we use raw neighbors but apply a threshold to the sharpening weight.
                if (u_Sharpness > 0.0) {
                    float peak = -1.0 / mix(8.0, 5.0, u_Sharpness);
                    vec3 e = color;
                    
                    vec3 minRGB = min(min(min(min(b, d), f), h), e);
                    vec3 maxRGB = max(max(max(max(b, d), f), h), e);
                    
                    // Modify contrast detect to ignore debanding "artifacts"
                    // min(minRGB, 1.0 - maxRGB) - 0.02 avoids sharpening low-contrast gradients
                    vec3 amp = clamp((min(minRGB, 1.0 - maxRGB) - 0.02) / max(maxRGB, 0.01), 0.0, 1.0);
                    amp = sqrt(amp);
                    float w = peak * amp.r; 
                    
                    color = clamp(( (b + d + f + h) * w + e) / (4.0 * w + 1.0), 0.0, 1.0);
                }

                // 3. Dithering
                float noise = (ign(v_TexCoord + frameSeed) - 0.5) * DITHER_GRAIN;
                color += vec3(noise);

                outColor = vec4(color, 1.0);
            }
        """

        // Fullscreen quad vertices (position + texcoord)
        private val QUAD_VERTICES = floatArrayOf(
            // X,    Y,    U,    V
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f
        )
        private const val COORDS_PER_VERTEX = 4
        private const val VERTEX_STRIDE = COORDS_PER_VERTEX * 4 // 4 bytes per float
    }

    // OpenGL programs
    private var copyProgram = 0
    private var effectProgram = 0
    
    // Textures and FBO
    private var oesTextureId = 0
    private var fboTextureId = 0
    private var fboId = 0
    
    private var vertexBuffer: FloatBuffer? = null

    // Pass 1 (Copy) locations
    private var uCopySTMatrixLoc = 0
    private var uCopyTextureLoc = 0
    private var aCopyPosLoc = 0
    private var aCopyTexLoc = 0
    
    // Pass 2 (Effect) locations
    private var uEffTextureLoc = 0
    private var uEffTimeLoc = 0
    private var uEffScreenSizeLoc = 0
    private var uEffSharpnessLoc = 0
    private var aEffPosLoc = 0
    private var aEffTexLoc = 0

    // SurfaceTexture and Surface for MediaCodec output
    private var surfaceTexture: SurfaceTexture? = null
    private var surface: Surface? = null

    // Texture transform matrix
    private val stMatrix = FloatArray(16)

    var sharpness = 0f

    // Screen dimensions
    private var screenWidth = 1
    private var screenHeight = 1

    @Volatile
    private var frameAvailable = false
    private var frameCount = 0f

    init {
        android.opengl.Matrix.setIdentityM(stMatrix, 0)
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        // 1. Create copy program
        copyProgram = createProgram(COPY_VERTEX_SHADER, COPY_FRAGMENT_SHADER)
        aCopyPosLoc = GLES30.glGetAttribLocation(copyProgram, "a_Position")
        aCopyTexLoc = GLES30.glGetAttribLocation(copyProgram, "a_TexCoord")
        uCopySTMatrixLoc = GLES30.glGetUniformLocation(copyProgram, "u_STMatrix")
        uCopyTextureLoc = GLES30.glGetUniformLocation(copyProgram, "u_Texture")

        // 2. Create effect program
        effectProgram = createProgram(EFFECT_VERTEX_SHADER, EFFECT_FRAGMENT_SHADER)
        aEffPosLoc = GLES30.glGetAttribLocation(effectProgram, "a_Position")
        aEffTexLoc = GLES30.glGetAttribLocation(effectProgram, "a_TexCoord")
        uEffTextureLoc = GLES30.glGetUniformLocation(effectProgram, "u_Texture")
        uEffTimeLoc = GLES30.glGetUniformLocation(effectProgram, "u_Time")
        uEffScreenSizeLoc = GLES30.glGetUniformLocation(effectProgram, "u_ScreenSize")
        uEffSharpnessLoc = GLES30.glGetUniformLocation(effectProgram, "u_Sharpness")

        // 3. Create OES texture
        val textures = IntArray(2)
        GLES30.glGenTextures(2, textures, 0)
        oesTextureId = textures[0]
        fboTextureId = textures[1]

        GLES30.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId)
        GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        
        // 4. Create FBO
        val fbos = IntArray(1)
        GLES30.glGenFramebuffers(1, fbos, 0)
        fboId = fbos[0]

        // 5. Create SurfaceTexture
        surfaceTexture = SurfaceTexture(oesTextureId).also {
            it.setOnFrameAvailableListener(this)
            surface = Surface(it)
            onSurfaceReady(surface!!)
        }

        // Create vertex buffer
        vertexBuffer = ByteBuffer.allocateDirect(QUAD_VERTICES.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(QUAD_VERTICES)
            .also { it.position(0) }

        GLES30.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        GLES30.glViewport(0, 0, width, height)
        screenWidth = width
        screenHeight = height
        surfaceTexture?.setDefaultBufferSize(width, height)
        
        // Resize FBO texture
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, fboTextureId)
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, width, height, 0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)

        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fboId)
        GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0, GLES30.GL_TEXTURE_2D, fboTextureId, 0)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
    }

    override fun onDrawFrame(gl: GL10?) {
        if (frameAvailable) {
            surfaceTexture?.updateTexImage()
            surfaceTexture?.getTransformMatrix(stMatrix)
            frameAvailable = false
        }

        // --- PASS 1: OES to FBO ---
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fboId)
        GLES30.glViewport(0, 0, screenWidth, screenHeight)
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(copyProgram)

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId)
        GLES30.glUniform1i(uCopyTextureLoc, 0)
        GLES30.glUniformMatrix4fv(uCopySTMatrixLoc, 1, false, stMatrix, 0)

        vertexBuffer?.position(0)
        GLES30.glVertexAttribPointer(aCopyPosLoc, 2, GLES30.GL_FLOAT, false, VERTEX_STRIDE, vertexBuffer)
        GLES30.glEnableVertexAttribArray(aCopyPosLoc)
        vertexBuffer?.position(2)
        GLES30.glVertexAttribPointer(aCopyTexLoc, 2, GLES30.GL_FLOAT, false, VERTEX_STRIDE, vertexBuffer)
        GLES30.glEnableVertexAttribArray(aCopyTexLoc)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)

        // --- PASS 2: FBO to Screen (Effects) ---
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        GLES30.glViewport(0, 0, screenWidth, screenHeight)
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
        GLES30.glUseProgram(effectProgram)

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, fboTextureId)
        GLES30.glUniform1i(uEffTextureLoc, 0)
        GLES30.glUniform1f(uEffTimeLoc, frameCount)
        GLES30.glUniform2f(uEffScreenSizeLoc, screenWidth.toFloat(), screenHeight.toFloat())
        GLES30.glUniform1f(uEffSharpnessLoc, sharpness)

        vertexBuffer?.position(0)
        GLES30.glVertexAttribPointer(aEffPosLoc, 2, GLES30.GL_FLOAT, false, VERTEX_STRIDE, vertexBuffer)
        GLES30.glEnableVertexAttribArray(aEffPosLoc)
        vertexBuffer?.position(2)
        GLES30.glVertexAttribPointer(aEffTexLoc, 2, GLES30.GL_FLOAT, false, VERTEX_STRIDE, vertexBuffer)
        GLES30.glEnableVertexAttribArray(aEffTexLoc)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)

        frameCount += 1.0f
        if (frameCount > 1000000f) frameCount = 0f

        GLES30.glDisableVertexAttribArray(aEffPosLoc)
        GLES30.glDisableVertexAttribArray(aEffTexLoc)
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        frameAvailable = true
    }

    fun release() {
        surface?.release()
        surfaceTexture?.release()
        if (copyProgram != 0) GLES30.glDeleteProgram(copyProgram)
        if (effectProgram != 0) GLES30.glDeleteProgram(effectProgram)
        GLES30.glDeleteTextures(2, intArrayOf(oesTextureId, fboTextureId), 0)
        GLES30.glDeleteFramebuffers(1, intArrayOf(fboId), 0)
    }

    private fun createProgram(vertexSource: String, fragmentSource: String): Int {
        val vertexShader = loadShader(GLES30.GL_VERTEX_SHADER, vertexSource)
        if (vertexShader == 0) return 0

        val fragmentShader = loadShader(GLES30.GL_FRAGMENT_SHADER, fragmentSource)
        if (fragmentShader == 0) return 0

        val program = GLES30.glCreateProgram()
        if (program == 0) return 0

        GLES30.glAttachShader(program, vertexShader)
        GLES30.glAttachShader(program, fragmentShader)
        GLES30.glLinkProgram(program)

        val linkStatus = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, linkStatus, 0)
        if (linkStatus[0] != GLES30.GL_TRUE) {
            val log = GLES30.glGetProgramInfoLog(program)
            GLES30.glDeleteProgram(program)
            throw RuntimeException("Program link failed: $log")
        }

        return program
    }

    private fun loadShader(type: Int, source: String): Int {
        val shader = GLES30.glCreateShader(type)
        if (shader == 0) return 0

        GLES30.glShaderSource(shader, source)
        GLES30.glCompileShader(shader)

        val compileStatus = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, compileStatus, 0)
        if (compileStatus[0] != GLES30.GL_TRUE) {
            val log = GLES30.glGetShaderInfoLog(shader)
            GLES30.glDeleteShader(shader)
            throw RuntimeException("Shader compile failed: $log")
        }

        return shader
    }
}
