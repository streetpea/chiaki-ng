// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import android.view.View
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.metallic.chiaki.R
import com.metallic.chiaki.common.RegisteredHost
import com.metallic.chiaki.common.ext.inflate
import kotlinx.android.synthetic.main.item_registered_host.view.*

class SettingsRegisteredHostsAdapter: RecyclerView.Adapter<SettingsRegisteredHostsAdapter.ViewHolder>()
{
	class ViewHolder(itemView: View): RecyclerView.ViewHolder(itemView)

	var hosts: List<RegisteredHost> = listOf()
		set(value)
		{
			field = value
			notifyDataSetChanged()
		}

	override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) = ViewHolder(parent.inflate(R.layout.item_registered_host))

	override fun getItemCount() = hosts.size

	override fun onBindViewHolder(holder: ViewHolder, position: Int)
	{
		val view = holder.itemView
		val host = hosts[position]
		view.nameTextView.text = "${host.serverNickname} (${if(host.target.isPS5) "PS5" else "PS4"})"
		view.summaryTextView.text = host.serverMac.toString()
	}
}