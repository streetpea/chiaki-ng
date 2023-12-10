// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGSDIALOG_H
#define CHIAKI_SETTINGSDIALOG_H

#include <QDialog>

class Settings;
class QListWidget;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QFormLayout;
#if CHIAKI_GUI_ENABLE_SPEEX
class QSlider;
class QLabel;
#endif

class SettingsDialog : public QDialog
{
	Q_OBJECT

	private:
		Settings *settings;

		QCheckBox *log_verbose_check_box;
		QComboBox *disconnect_action_combo_box;
		QCheckBox *dualsense_check_box;
		QCheckBox *buttons_pos_check_box;
		QCheckBox *vertical_sdeck_check_box;
		QCheckBox *automatic_connect_check_box;

		QComboBox *resolution_combo_box;
		QComboBox *fps_combo_box;
		QLineEdit *bitrate_edit;
		QComboBox *codec_combo_box;
		QLineEdit *audio_buffer_size_edit;
		QComboBox *audio_device_combo_box;
		QComboBox *microphone_combo_box;
		QCheckBox *pi_decoder_check_box;
		QComboBox *hw_decoder_combo_box;
		QComboBox *renderer_combo_box;
#if CHIAKI_GUI_ENABLE_PLACEBO
		QComboBox *placebo_preset_combo_box;

		QFormLayout *renderer_settings_layout;
#endif
#if CHIAKI_GUI_ENABLE_SPEEX
		QCheckBox *speech_processing_check_box;
		QSlider *noiseSuppress;
		QSlider *echoSuppress;
		QLabel *echoValue;
		QLabel *noiseValue;
#endif

		QListWidget *registered_hosts_list_widget;
		QPushButton *delete_registered_host_button;

		void UpdateBitratePlaceholder();

	private slots:
		void LogVerboseChanged();
		void DualSenseChanged();
		void ButtonsPosChanged();
		void DeckOrientationChanged();
		void AutomaticConnectChanged();
#if CHIAKI_GUI_ENABLE_SPEEX
		void SpeechProcessingChanged();
#endif
		void DisconnectActionSelected();

		void ResolutionSelected();
		void FPSSelected();
		void BitrateEdited();
		void CodecSelected();
		void AudioBufferSizeEdited();
		void AudioOutputSelected();
		void AudioInputSelected();
		void HardwareDecodeEngineSelected();
		void UpdateHardwareDecodeEngineComboBox();
		void RendererSelected();
#if CHIAKI_GUI_ENABLE_PLACEBO
		void PlaceboPresetSelected();
#endif

		void UpdateRegisteredHosts();
		void UpdateRegisteredHostsButtons();
		void RegisterNewHost();
		void DeleteRegisteredHost();

	public:
		SettingsDialog(Settings *settings, QWidget *parent = nullptr);
};

#endif // CHIAKI_SETTINGSDIALOG_H
