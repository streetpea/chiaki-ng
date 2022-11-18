// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CONTROLLERMANAGER_H
#define CHIAKI_CONTROLLERMANAGER_H

#include <chiaki/controller.h>

#include <QObject>
#include <QSet>
#include <QMap>
#include <QString>

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
#include <SDL.h>
#endif

class Controller;

class ControllerManager : public QObject
{
	Q_OBJECT

	friend class Controller;

	private:
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		QSet<SDL_JoystickID> available_controllers;
#endif
		QMap<int, Controller *> open_controllers;

		void ControllerClosed(Controller *controller);

	private slots:
		void UpdateAvailableControllers();
		void HandleEvents();
		void ControllerEvent(int device_id);

	public:
		static ControllerManager *GetInstance();

		ControllerManager(QObject *parent = nullptr);
		~ControllerManager();

		QSet<int> GetAvailableControllers();
		Controller *OpenController(int device_id);

	signals:
		void AvailableControllersUpdated();
};

class Controller : public QObject
{
	Q_OBJECT

	friend class ControllerManager;

	private:
		Controller(int device_id, ControllerManager *manager);

		void UpdateState();

		ControllerManager *manager;
		int id;

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		SDL_GameController *controller;
#endif

	public:
		~Controller();

		bool IsConnected();
		int GetDeviceID();
		QString GetName();
		ChiakiControllerState GetState();
		void SetRumble(uint8_t left, uint8_t right);
		void SetTriggerEffects(uint8_t type_left, const uint8_t *data_left, uint8_t type_right, const uint8_t *data_right);

	signals:
		void StateChanged();
};

/* PS5 trigger effect documentation:
   https://controllers.fandom.com/wiki/Sony_DualSense#FFB_Trigger_Modes

   Taken from SDL2, licensed under the zlib license,
   Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
   https://github.com/libsdl-org/SDL/blob/release-2.24.1/test/testgamecontroller.c#L263-L289
*/
typedef struct
{
    Uint8 ucEnableBits1;                /* 0 */
    Uint8 ucEnableBits2;                /* 1 */
    Uint8 ucRumbleRight;                /* 2 */
    Uint8 ucRumbleLeft;                 /* 3 */
    Uint8 ucHeadphoneVolume;            /* 4 */
    Uint8 ucSpeakerVolume;              /* 5 */
    Uint8 ucMicrophoneVolume;           /* 6 */
    Uint8 ucAudioEnableBits;            /* 7 */
    Uint8 ucMicLightMode;               /* 8 */
    Uint8 ucAudioMuteBits;              /* 9 */
    Uint8 rgucRightTriggerEffect[11];   /* 10 */
    Uint8 rgucLeftTriggerEffect[11];    /* 21 */
    Uint8 rgucUnknown1[6];              /* 32 */
    Uint8 ucLedFlags;                   /* 38 */
    Uint8 rgucUnknown2[2];              /* 39 */
    Uint8 ucLedAnim;                    /* 41 */
    Uint8 ucLedBrightness;              /* 42 */
    Uint8 ucPadLights;                  /* 43 */
    Uint8 ucLedRed;                     /* 44 */
    Uint8 ucLedGreen;                   /* 45 */
    Uint8 ucLedBlue;                    /* 46 */
} DS5EffectsState_t;

#endif // CHIAKI_CONTROLLERMANAGER_H
