/*
    Copyright (C) 2019 Doug McLain

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DUDESTARRX_H
#define DUDESTARRX_H

#include <QMainWindow>
#include <QtNetwork>
#include <QAudioOutput>
#include <QTimer>
#include <QLabel>
#include "mbe.h"
#include "ysf.h"

namespace Ui {
class DudeStarRX;
}

class DudeStarRX : public QMainWindow
{
	Q_OBJECT

public:
	explicit DudeStarRX(QWidget *parent = nullptr);
	~DudeStarRX();

private:
	void init_gui();
	Ui::DudeStarRX *ui;
	QUdpSocket *udp = nullptr;
	enum{
		DISCONNECTED,
		CONNECTING,
		CONNECTED_RW,
		CONNECTED_RO
	} connect_status;
	QUrl hosts_site;
	QNetworkAccessManager qnam;
	QNetworkReply *reply;
	bool httpRequestAborted;
	QString host;
	int port;
	QHostAddress address;
	QString callsign;
	QString serial;
	char module;
	QString protocol;
	uint64_t ping_cnt;
	MBEDecoder *mbe;
	DSDYSF *ysf;
	QAudioOutput *audio;
	QIODevice *audiodev;
	QByteArray user_data;
	QTimer *audiotimer;
	QTimer *ysftimer;
	QTimer *ping_timer;
	QString config_path;
	QString hosts_filename;
	QLabel *status_txt;
	QQueue<unsigned char> audioq;
	QQueue<unsigned char> ysfq;

	const unsigned char header[5] = {0x80,0x44,0x53,0x56,0x54}; //DVSI packet header
private slots:
	void about();
	void process_connect();
	void readyRead();
	void readyReadREF();
	void readyReadXRF();
	void readyReadDCS();
	void readyReadXLX();
	void readyReadYSF();
	void disconnect_from_host();
	void handleStateChanged(QAudio::State);
	void hostname_lookup(QHostInfo);
	void process_audio();
	void process_ysf_data();
	void process_ref_hosts();
	void process_dcs_hosts();
	void process_xrf_hosts();
	void process_ysf_hosts();
	void process_mode_change(const QString &);
	void process_settings();
	void process_ping();
	void load_hosts_file();
	void start_request(QString);
	void http_finished(QNetworkReply *reply);
};

#endif // DUDESTARRX_H
