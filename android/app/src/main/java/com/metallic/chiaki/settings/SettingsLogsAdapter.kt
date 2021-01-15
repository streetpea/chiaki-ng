// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.metallic.chiaki.R
import com.metallic.chiaki.common.LogFile
import com.metallic.chiaki.common.ext.inflate
import com.metallic.chiaki.databinding.ItemLogFileBinding
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.*

class SettingsLogsAdapter: RecyclerView.Adapter<SettingsLogsAdapter.ViewHolder>()
{
	var shareCallback: ((LogFile) -> Unit)? = null

	private val dateFormat: DateFormat = DateFormat.getDateInstance(DateFormat.SHORT)
	private val timeFormat = SimpleDateFormat("HH:mm:ss:SSS", Locale.getDefault())

	class ViewHolder(val binding: ItemLogFileBinding): RecyclerView.ViewHolder(binding.root)

	var logFiles: List<LogFile> = listOf()
		set(value)
		{
			field = value
			notifyDataSetChanged()
		}

	override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) =
		ViewHolder(ItemLogFileBinding.inflate(LayoutInflater.from(parent.context), parent, false))

	override fun getItemCount() = logFiles.size

	override fun onBindViewHolder(holder: ViewHolder, position: Int)
	{
		val logFile = logFiles[position]
		holder.binding.nameTextView.text = "${dateFormat.format(logFile.date)} ${timeFormat.format(logFile.date)}"
		holder.binding.summaryTextView.text = logFile.filename
		holder.binding.shareButton.setOnClickListener { shareCallback?.let { it(logFile) } }
	}
}