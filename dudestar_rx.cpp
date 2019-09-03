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

#include "dudestar_rx.h"
#include "ui_dudestar_rx.h"
#include "SHA256.h"
#include "crs129.h"
#include "cbptc19696.h"
#include "cgolay2087.h"
#include <iostream>
#include <QMessageBox>
#include <QFileDialog>

#define LOBYTE(w)				((uint8_t)(uint16_t)(w & 0x00FF))
#define HIBYTE(w)				((uint8_t)((((uint16_t)(w)) >> 8) & 0xFF))
#define LOWORD(dw)				((uint16_t)(uint32_t)(dw & 0x0000FFFF))
#define HIWORD(dw)				((uint16_t)((((uint32_t)(dw)) >> 16) & 0xFFFF))
#define DEBUG
//define DEBUG_YSF

DudeStarRX::DudeStarRX(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::DudeStarRX)
{
	ping_cnt = 0;
	ui->setupUi(this);
	init_gui();
	config_path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	config_path += "/dudestar_rx";
	connect(&qnam, SIGNAL(finished(QNetworkReply*)), this, SLOT(http_finished(QNetworkReply*)));

	QAudioFormat format;
	format.setSampleRate(8000);
	format.setChannelCount(1);
	format.setSampleSize(16);
	format.setCodec("audio/pcm");
	format.setByteOrder(QAudioFormat::LittleEndian);
	format.setSampleType(QAudioFormat::SignedInt);
	QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);

	if(devices.size() == 0){
		qDebug() << "No audio hardware found";
	}
	else{
		QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
		QList<int> srs = info.supportedSampleRates();
		for(int i = 0; i < srs.size(); ++i){
			qDebug() << "Sample rate " << srs[i] << " supported";
		}
		QList<int> ss = info.supportedSampleSizes();
		for(int i = 0; i < ss.size(); ++i){
			qDebug() << "Sample size " << ss[i] << " supported";
		}

		if (!info.isFormatSupported(format)) {
			qWarning() << "Raw audio format not supported by backend, trying nearest format.";
			format = info.nearestFormat(format);
			qWarning() << "Format now set to " << format.sampleRate() << ":" << format.sampleSize();
		}
	}
	audio = new QAudioOutput(format, this);
	audiotimer = new QTimer();
	ping_timer = new QTimer();
	ysftimer = new QTimer();
	connect(audio, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));

	connect_status = DISCONNECTED;

	connect(audiotimer, SIGNAL(timeout()), this, SLOT(process_audio()));
	connect(ysftimer, SIGNAL(timeout()), this, SLOT(process_ysf_data()));
	connect(ping_timer, SIGNAL(timeout()), this, SLOT(process_ping()));
	audiotimer->start(19);
	ysftimer->start(90);
	process_settings();
}

DudeStarRX::~DudeStarRX()
{
	QFile f(config_path + "/settings.conf");
	f.open(QIODevice::WriteOnly);
	QTextStream stream(&f);
	stream << "MODE:" << ui->modeCombo->currentText() << endl;
	stream << "HOST:" << ui->hostCombo->currentText() << endl;
	stream << "MODULE:" << ui->comboMod->currentText() << endl;
	stream << "CALLSIGN:" << ui->callsignEdit->text() << endl;
	stream << "DMRTGID:" << ui->dmrtgEdit->text() << endl;
	f.close();
	delete ui;
}

void DudeStarRX::about()
{
	QMessageBox::about(this, tr("About DUDE-Star RX"), tr("DUDE-Star RX v0.10-beta\nCopyright (C) 2019 Doug McLain AD8DP\n\n"
															"This program is free software; you can redistribute it and/or modify"
															" it under the terms of the GNU General Public License as published by "
															"the Free Software Foundation; version 2.\n\nThis program is distributed"
															" in the hope that it will be useful, but WITHOUT ANY WARRANTY; without "
															"even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR "
															"PURPOSE. See the GNU General Public License for more details.\n\nYou should"
															" have received a copy of the GNU General Public License along with this program. "
															"If not, see <http://www.gnu.org/licenses/>"));
}

void DudeStarRX::init_gui()
{
	status_txt = new QLabel("Not connected");
	connect(ui->actionAbout, SIGNAL(triggered()), this, SLOT(about()));
	connect(ui->actionQuit, SIGNAL(triggered()), this, SLOT(close()));
	//connect(ui->actionLoad_hosts_file, SIGNAL(triggered()), this, SLOT(load_hosts_file()));
	//connect(ui->actionDownload_hosts_file, SIGNAL(triggered()), this, SLOT(start_request()));
	connect(ui->connectButton, SIGNAL(clicked()), this, SLOT(process_connect()));
	ui->statusBar->insertPermanentWidget(0, status_txt, 1);
	ui->callsignEdit->setMaximumWidth(60);
	ui->rptr1->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->rptr2->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->mycall->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->urcall->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->streamid->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->usertxt->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->modeCombo->addItem("REF");
	ui->modeCombo->addItem("DCS");
	ui->modeCombo->addItem("XRF");
	ui->modeCombo->addItem("YSF");
	ui->modeCombo->addItem("DMR");
	connect(ui->modeCombo, SIGNAL(currentTextChanged(const QString &)), this, SLOT(process_mode_change(const QString &)));

	for(char m = 0x41; m < 0x5b; ++m){
		ui->comboMod->addItem(QString(m));
	}

	ui->hostCombo->setEditable(true);
	ui->dmrtgEdit->setEnabled(false);
}

void DudeStarRX::start_request(QString f)
{
	hosts_filename = f;
	qnam.get(QNetworkRequest(QUrl("http://www.dudetronics.com/ar-dns" + f)));
	status_txt->setText(tr("Downloading http://www.dudetronics.com/ar-dns" + f.toLocal8Bit()));
}

void DudeStarRX::http_finished(QNetworkReply *reply)
{
	if (reply->error()) {
		status_txt->setText(tr("Download failed:\n%1.").arg(reply->errorString()));
		reply->deleteLater();
		reply = nullptr;
		return;
	}
	else{
		QFile *hosts_file = new QFile(config_path + hosts_filename);
		hosts_file->open(QIODevice::WriteOnly);
		QFileInfo fileInfo(hosts_file->fileName());
		QString filename(fileInfo.fileName());
		hosts_file->write(reply->readAll());
		hosts_file->flush();
		hosts_file->close();
		//qnam.clearAccessCache();
		//qnam.clearConnectionCache();
		delete hosts_file;
		status_txt->setText(tr("Downloaded " + filename.toLocal8Bit()));

		if(filename == "dplus.txt"){
			process_ref_hosts();
		}
		else if(filename == "dextra.txt"){
			process_xrf_hosts();
		}
		else if(filename == "dcs.txt"){
			process_dcs_hosts();
		}
		else if(filename == "YSFHosts.txt"){
			process_ysf_hosts();
		}
		else if(filename == "DMRHosts.txt"){
			process_dmr_hosts();
		}
		else if(filename == "dmrids.txt"){
			process_dmr_ids();
		}
    }
}

void DudeStarRX::load_hosts_file()
{
	QString s = QFileDialog::getOpenFileName(this, tr("Open hosts file"));
	if(!s.isNull()){
		QFileInfo check_file(config_path + "/hosts");

		if(check_file.exists() && check_file.isFile()){
			QFile::remove(config_path + "/hosts");
		}

		QFile::copy(s, config_path + "/hosts");
		process_ref_hosts();
	}
}

void DudeStarRX::process_mode_change(const QString &m)
{
	if(m == "REF"){
		process_ref_hosts();
		ui->comboMod->setEnabled(true);
		ui->dmrtgEdit->setEnabled(false);
		ui->label_1->setText("MYCALL");
		ui->label_2->setText("URCALL");
		ui->label_3->setText("RPTR1");
		ui->label_4->setText("RPTR2");
		ui->label_5->setText("Stream ID");
		ui->label_6->setText("User txt");
	}
	if(m == "DCS"){
		process_dcs_hosts();
		ui->comboMod->setEnabled(true);
		ui->dmrtgEdit->setEnabled(false);
		ui->label_1->setText("MYCALL");
		ui->label_2->setText("URCALL");
		ui->label_3->setText("RPTR1");
		ui->label_4->setText("RPTR2");
		ui->label_5->setText("Stream ID");
		ui->label_6->setText("User txt");
	}
	if(m == "XRF"){
		process_xrf_hosts();
		ui->comboMod->setEnabled(true);
		ui->dmrtgEdit->setEnabled(false);
		ui->label_1->setText("MYCALL");
		ui->label_2->setText("URCALL");
		ui->label_3->setText("RPTR1");
		ui->label_4->setText("RPTR2");
		ui->label_5->setText("Stream ID");
		ui->label_6->setText("User txt");
	}
	else if(m == "YSF"){
		process_ysf_hosts();
		ui->comboMod->setEnabled(false);
		ui->dmrtgEdit->setEnabled(false);
		ui->label_1->setText("Gateway");
		ui->label_2->setText("Callsign");
		ui->label_3->setText("Dest");
		ui->label_4->setText("Type");
		ui->label_5->setText("Path");
		ui->label_6->setText("Frame#");
	}
	else if(m == "DMR"){
		process_dmr_hosts();
		process_dmr_ids();
		ui->comboMod->setEnabled(false);
		ui->dmrtgEdit->setEnabled(true);
		ui->label_1->setText("Callsign");
		ui->label_2->setText("SrcID");
		ui->label_3->setText("DestID");
		ui->label_4->setText("GWID");
		ui->label_5->setText("Seq#");
		ui->label_6->setText("");
	}
}

void DudeStarRX::process_ref_hosts()
{
	if(!QDir(config_path).exists()){
		QDir().mkdir(config_path);
	}

	QFileInfo check_file(config_path + "/dplus.txt");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/dplus.txt");
		if(f.open(QIODevice::ReadOnly)){
			ui->hostCombo->clear();
			while(!f.atEnd()){
				QString l = f.readLine();
				if(l.at(0) == '#'){
					continue;
				}
				QStringList ll = l.split('\t');
				ui->hostCombo->addItem(ll.at(0).simplified(), ll.at(1) + ":20001");
			}
		}
		f.close();
	}
	else{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "No DPlus file", "No DPlus file found, download?", QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			start_request("/dplus.txt");
		}
	}
}

void DudeStarRX::process_dcs_hosts()
{
	if(!QDir(config_path).exists()){
		QDir().mkdir(config_path);
	}

	QFileInfo check_file(config_path + "/dcs.txt");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/dcs.txt");
		if(f.open(QIODevice::ReadOnly)){
			ui->hostCombo->clear();
			while(!f.atEnd()){
				QString l = f.readLine();
				if(l.at(0) == '#'){
					continue;
				}
				QStringList ll = l.split('\t');
				ui->hostCombo->addItem(ll.at(0).simplified(), ll.at(1) + ":30051");
			}
		}
		f.close();
	}
	else{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "No DExtra file", "No DExtra file found, download?", QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			start_request("/dcs.txt");
		}
	}
}


void DudeStarRX::process_xrf_hosts()
{
	if(!QDir(config_path).exists()){
		QDir().mkdir(config_path);
	}

	QFileInfo check_file(config_path + "/dextra.txt");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/dextra.txt");
		if(f.open(QIODevice::ReadOnly)){
			ui->hostCombo->clear();
			while(!f.atEnd()){
				QString l = f.readLine();
				if(l.at(0) == '#'){
					continue;
				}
				QStringList ll = l.split('\t');
				ui->hostCombo->addItem(ll.at(0).simplified(), ll.at(1) + ":30001");
			}
		}
		f.close();
	}
	else{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "No DExtra file", "No DExtra file found, download?", QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			start_request("/dextra.txt");
		}
	}
}


void DudeStarRX::process_ysf_hosts()
{
	if(!QDir(config_path).exists()){
		QDir().mkdir(config_path);
	}

	QFileInfo check_file(config_path + "/YSFHosts.txt");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/YSFHosts.txt");
		if(f.open(QIODevice::ReadOnly)){
			ui->hostCombo->clear();
			while(!f.atEnd()){
				QString l = f.readLine();
				QStringList ll = l.split(';');
				ui->hostCombo->addItem(ll.at(1).simplified() + " - " + ll.at(2).simplified(), ll.at(3) + ":" + ll.at(4));
			}
		}
		f.close();
	}
	else{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "No YSFHosts file", "No YSFHosts file found, download?", QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			start_request("/YSFHosts.txt");
		}
	}
}

void DudeStarRX::process_dmr_hosts()
{
	if(!QDir(config_path).exists()){
		QDir().mkdir(config_path);
	}

	QFileInfo check_file(config_path + "/DMRHosts.txt");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/DMRHosts.txt");
		if(f.open(QIODevice::ReadOnly)){
			ui->hostCombo->clear();
			while(!f.atEnd()){
				QString l = f.readLine();
				if(l.at(0) == '#'){
					continue;
				}
				QStringList ll = l.simplified().split(' ');
				if(ll.size() == 5){
					//qDebug() << ll.at(0).simplified() << " " <<  ll.at(2) + ":" + ll.at(4);
					ui->hostCombo->addItem(ll.at(0).simplified(), ll.at(2) + ":" + ll.at(4) + ":" + ll.at(3));
				}
			}
		}
		f.close();
	}
	else{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "No DMRHosts file", "No DMRHosts file found, download?", QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			start_request("/DMRHosts.txt");
		}
	}
}

void DudeStarRX::process_dmr_ids()
{
	if(!QDir(config_path).exists()){
		QDir().mkdir(config_path);
	}

	QFileInfo check_file(config_path + "/dmrids.txt");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/dmrids.txt");
		if(f.open(QIODevice::ReadOnly)){
			while(!f.atEnd()){
				QString l = f.readLine();
				if(l.at(0) == '#'){
					continue;
				}
				QStringList ll = l.simplified().split(';');
				//qDebug() << ll.at(0).simplified() << " " <<  ll.at(2) + ":" + ll.at(4);
				dmrids[ll.at(0).toUInt()] = ll.at(1);
			}
		}
		f.close();
	}
	else{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "No DMR ID file", "No DMR ID file found, download?", QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::Yes) {
			start_request("/dmrids.txt");
		}
	}
}

void DudeStarRX::process_settings()
{
	QFileInfo check_file(config_path + "/settings.conf");
	if(check_file.exists() && check_file.isFile()){
		QFile f(config_path + "/settings.conf");
		if(f.open(QIODevice::ReadOnly)){
			while(!f.atEnd()){
				QString s = f.readLine();
				QStringList sl = s.split(':');
				if(sl.at(0) == "MODE"){
					int i = ui->modeCombo->findText(sl.at(1).simplified());
					ui->modeCombo->setCurrentIndex(i);
					if(i == 0){
						process_ref_hosts();
					}
					else if(i == 1){
						process_dcs_hosts();
					}
					else if(i == 2){
						process_xrf_hosts();
					}
					else if(i == 3){
						process_ysf_hosts();
					}
					else if(i == 4){
						process_dmr_hosts();
					}
				}

				if(sl.at(0) == "HOST"){
					int i = ui->hostCombo->findText(sl.at(1).simplified());
					ui->hostCombo->setCurrentIndex(i);
				}
				if(sl.at(0) == "MODULE"){
					ui->comboMod->setCurrentText(sl.at(1).simplified());
				}
				if(sl.at(0) == "CALLSIGN"){
					ui->callsignEdit->setText(sl.at(1).simplified());
				}
				if(sl.at(0) == "DMRTGID"){
					ui->dmrtgEdit->setText(sl.at(1).simplified());
				}
			}
		}
	}
	else{ //No settings.conf file, first time launch
		process_ref_hosts();
	}
}

void DudeStarRX::disconnect_from_host()
{
	QByteArray d;
	if(protocol == "REF"){
		d[0] = 0x05;
		d[1] = 0x00;
		d[2] = 0x18;
		d[3] = 0x00;
		d[4] = 0x00;
	}
	if(protocol == "XRF"){
		d.append(callsign);
		d.append(8 - callsign.size(), ' ');
		d[8] = module;
		d[9] = ' ';
		d[10] = 0;
	}
	if(protocol == "DCS"){
		d.append(callsign);
		d.append(8 - callsign.size(), ' ');
		d[8] = module;
		d[9] = ' ';
		d[10] = 0;
	}
	else if(protocol == "XLX"){
		d[0] = 'R';
		d[1] = 'P';
		d[2] = 'T';
		d[3] = 'L';
		d[4] = (dmrid >> 24) & 0xff;
		d[5] = (dmrid >> 16) & 0xff;
		d[6] = (dmrid >> 8) & 0xff;
		d[7] = (dmrid >> 0) & 0xff;
		ping_timer->stop();

	}
	else if(protocol == "YSF"){
		d[0] = 'Y';
		d[1] = 'S';
		d[2] = 'F';
		d[3] = 'U';
		d.append(callsign);
		d.append(5, ' ');
		ping_timer->stop();
	}
	else if(protocol == "DMR"){
		d[0] = 'R';
		d[1] = 'P';
		d[2] = 'T';
		d[3] = 'C';
		d[4] = 'L';
		d[5] = (dmrid >> 24) & 0xff;
		d[6] = (dmrid >> 16) & 0xff;
		d[7] = (dmrid >> 8) & 0xff;
		d[8] = (dmrid >> 0) & 0xff;
		ui->dmrtgEdit->setEnabled(true);
		dmr_header_timer->stop();
		ping_timer->stop();

	}
	udp->writeDatagram(d, QHostAddress(host), port);
	//disconnect(udp, SIGNAL(readyRead()));
	udp->disconnect();
	udp->close();
	delete udp;
}

void DudeStarRX::process_connect()
{
	if(connect_status != DISCONNECTED){
		connect_status = DISCONNECTED;
		ping_cnt = 0;
		ui->connectButton->setText("Connect");
		ui->mycall->clear();
		ui->urcall->clear();
		ui->rptr1->clear();
		ui->rptr2->clear();
		ui->modeCombo->setEnabled(true);
		ui->hostCombo->setEnabled(true);
		ui->callsignEdit->setEnabled(true);
		disconnect_from_host();
		status_txt->setText("Not connected");
	}
	else{
		hostname = ui->hostCombo->currentText().simplified();
		QStringList sl = ui->hostCombo->currentData().toString().simplified().split(':');
		connect_status = CONNECTING;
		status_txt->setText("Connecting...");
		ui->connectButton->setEnabled(false);
		ui->connectButton->setText("Connecting");
		host = sl.at(0).simplified();
		port = sl.at(1).toInt();
		callsign = ui->callsignEdit->text();
		module = ui->comboMod->currentText().toStdString()[0];
		protocol = ui->modeCombo->currentText();
		if(protocol == "DMR"){
			dmrid = dmrids.key(callsign);
			dmr_password = sl.at(2).simplified();
		}
		QHostInfo::lookupHost(host, this, SLOT(hostname_lookup(QHostInfo)));
		audiodev = audio->start();
	}
}

void DudeStarRX::hostname_lookup(QHostInfo i)
{
	QByteArray d;
	if(protocol == "REF"){
		d[0] = 0x05;
		d[1] = 0x00;
		d[2] = 0x18;
		d[3] = 0x00;
		d[4] = 0x01;
	}
	if(protocol == "XRF"){
		d.append(callsign);
		d.append(8 - callsign.size(), ' ');
		d[8] = module;
		d[9] = module;
		d[10] = 11;
	}
	if(protocol == "DCS"){
		d.append(callsign);
		d.append(8 - callsign.size(), ' ');
		d[8] = module;
		d[9] = module;
		d[10] = 11;
		d.append(508, 0);
	}
	else if(protocol == "XLX"){
		d[0] = 'R';
		d[1] = 'P';
		d[2] = 'T';
		d[3] = 'L';
		d[4] = (dmrid >> 24) & 0xff;
		d[5] = (dmrid >> 16) & 0xff;
		d[6] = (dmrid >> 8) & 0xff;
		d[7] = (dmrid >> 0) & 0xff;
	}
	else if(protocol == "YSF"){
		d[0] = 'Y';
		d[1] = 'S';
		d[2] = 'F';
		d[3] = 'P';
		d.append(callsign);
		d.append(5, ' ');
	}
	else if(protocol == "DMR"){
		d[0] = 'R';
		d[1] = 'P';
		d[2] = 'T';
		d[3] = 'L';
		d[4] = (dmrid >> 24) & 0xff;
		d[5] = (dmrid >> 16) & 0xff;
		d[6] = (dmrid >> 8) & 0xff;
		d[7] = (dmrid >> 0) & 0xff;
	}
	if (!i.addresses().isEmpty()) {
		address = i.addresses().first();
		udp = new QUdpSocket(this);
		connect(udp, SIGNAL(readyRead()), this, SLOT(readyRead()));
		udp->writeDatagram(d, address, port);
	}
}

void DudeStarRX::process_audio()
{
	int nbAudioSamples = 0;
	short *audioSamples;
	unsigned char d[9];

	if(audioq.size() < 9){
		return;
	}
	if(!mbe){
		return;
	}

	for(int i = 0; i < 9; ++i){
		d[i] = audioq.dequeue();
	}
	if(protocol == "DMR"){
		mbe->process_dmr(d);
	}
	else{
		mbe->process_dstar(d);
	}
	audioSamples = mbe->getAudio(nbAudioSamples);
	audiodev->write((const char *) audioSamples, sizeof(short) * nbAudioSamples);
	mbe->resetAudio();
}

void DudeStarRX::AppendVoiceLCToBuffer(QByteArray& buffer, uint32_t uiSrcId, uint32_t uiDstId) const
{
	uint8_t g_DmrSyncBSData[]     = { 0x0D,0xFF,0x57,0xD7,0x5D,0xF5,0xD0 };
	uint8_t g_DmrSyncMSData[]     = { 0x0D,0x5D,0x7F,0x77,0xFD,0x75,0x70 };
	uint8_t payload[33];

	// fill payload
	CBPTC19696 bptc;
	::memset(payload, 0, sizeof(payload));
	// LC data
	uint8_t lc[12];
	{
		::memset(lc, 0, sizeof(lc));
		lc[3] = (uint8_t)LOBYTE(HIWORD(uiDstId));
		lc[4] = (uint8_t)HIBYTE(LOWORD(uiDstId));
		lc[5] = (uint8_t)LOBYTE(LOWORD(uiDstId));
		// uiSrcId
		lc[6] = (uint8_t)LOBYTE(HIWORD(uiSrcId));
		lc[7] = (uint8_t)HIBYTE(LOWORD(uiSrcId));
		lc[8] = (uint8_t)LOBYTE(LOWORD(uiSrcId));
		// parity
		uint8_t parity[4];
		CRS129::encode(lc, 9, parity);
		lc[9]  = parity[2] ^ 0x96;
		lc[10] = parity[1] ^ 0x96;
		lc[11] = parity[0] ^ 0x96;
	}
	// sync
	::memcpy(payload+13, g_DmrSyncMSData, sizeof(g_DmrSyncMSData));
	// slot type
	{
		// slot type
		uint8_t slottype[3];
		::memset(slottype, 0, sizeof(slottype));
		slottype[0]  = (1 << 4) & 0xF0;
		slottype[0] |= (1  << 0) & 0x0FU;
		CGolay2087::encode(slottype);
		payload[12U] = (payload[12U] & 0xC0U) | ((slottype[0U] >> 2) & 0x3FU);
		payload[13U] = (payload[13U] & 0x0FU) | ((slottype[0U] << 6) & 0xC0U) | ((slottype[1U] >> 2) & 0x30U);
		payload[19U] = (payload[19U] & 0xF0U) | ((slottype[1U] >> 2) & 0x0FU);
		payload[20U] = (payload[20U] & 0x03U) | ((slottype[1U] << 6) & 0xC0U) | ((slottype[2U] >> 2) & 0x3CU);

	}
	// and encode
	bptc.encode(lc, payload);

	// and append
	buffer.append((char *)payload, sizeof(payload));
}

void DudeStarRX::tx_dmr_header()
{
	QByteArray out;
	dmr_destid = ui->dmrtgEdit->text().toInt();
	unsigned char dmrheader_tx[55] = {
		0x44, 0x4D, 0x52, 0x44, 0x00, 0x2F, 0xB4, 0xD2, 0x00, 0x00, 0x5B, 0x00, 0x2F, 0xB4, 0xD2, 0xA1,
		0xE9, 0xF5, 0x3A, 0x3A, 0x02, 0x46, 0x0C, 0x3C, 0x1E, 0xA4, 0x12, 0x20, 0x5F, 0x20, 0x04, 0x40,
		0x44, 0x6D, 0x5D, 0x7F, 0x77, 0xFD, 0x75, 0x7E, 0x33, 0x00, 0x00, 0xD0, 0x31, 0x20, 0x36, 0x40,
		0x1D, 0x81, 0xE8, 0x03, 0x84, 0x00, 0x00
	};
	//out.append((char *)dmrheader_tx, 55);

	out.append("DMRD", 4);
	out.append('\0');
	out[5] = (dmrid >> 16) & 0xff;
	out[6] = (dmrid >> 8) & 0xff;
	out[7] = (dmrid >> 0) & 0xff;
	out[8] = (dmr_destid >> 16) & 0xff;
	out[9] = (dmr_destid >> 8) & 0xff;
	out[10] = (dmr_destid >> 0) & 0xff;
	out[11] = (dmrid >> 24) & 0xff;
	out[12] = (dmrid >> 16) & 0xff;
	out[13] = (dmrid >> 8) & 0xff;
	out[14] = (dmrid >> 0) & 0xff;
	out[15] = 0xa1;
	out[16] = 0x0e;
	out[17] = 0x00;
	out[18] = 0x00;
	out[19] = 0x00;
	AppendVoiceLCToBuffer(out, dmrid, dmr_destid);
	out.append(2, 0);

	udp->writeDatagram(out, address, port);

	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);

}

void DudeStarRX::process_ysf_data()
{
	int nbAudioSamples = 0;
	short *audioSamples;
	unsigned char d[115];
	if(ysfq.size() < 115){
		//std::cerr << "process_ysf_data() no data" << std::endl;
		return;
	}
	for(int i = 0; i < 115; ++i){
		d[i] = ysfq.dequeue();
	}
	DSDYSF::FICH f = ysf->process_ysf(d);
	//std::cerr << "process_ysf_data() f: " << f << std::endl;
	audioSamples = ysf->getAudio(nbAudioSamples);
	if(f.getDataType() == 0){
		ui->rptr2->setText("V/D mode 1");
	}
	else if(f.getDataType() == 1){
		ui->rptr2->setText("Data Full Rate");
	}
	else if(f.getDataType() == 2){
		ui->rptr2->setText("V/D mode 2");
	}
	else if(f.getDataType() == 3){
		ui->rptr2->setText("Voice Full Rate");
	}
	ui->streamid->setText(f.isInternetPath() ? "Internet" : "Local");
	ui->usertxt->setText(QString::number(f.getFrameNumber()) + "/" + QString::number(f.getFrameTotal()));
	audiodev->write((const char *) audioSamples, sizeof(short) * nbAudioSamples);
	ysf->resetAudio();
}

void DudeStarRX::readyRead()
{
	if(protocol == "REF"){
		readyReadREF();
	}
	else if (protocol == "XLX"){
		readyReadXLX();
	}
	else if (protocol == "XRF"){
		readyReadXRF();
	}
	else if (protocol == "DCS"){
		readyReadDCS();
	}
	else if (protocol == "YSF"){
		readyReadYSF();
	}
	else if (protocol == "DMR"){
		readyReadDMR();
	}
}

void DudeStarRX::process_ping()
{
	QByteArray out;
	if(protocol == "XLX"){
		char tag[] = { 'R','P','T','P','I','N','G' };
		out.clear();
		out.append(tag, 7);
		out[7] = (dmrid >> 24) & 0xff;
		out[8] = (dmrid >> 16) & 0xff;
		out[9] = (dmrid >> 8) & 0xff;
		out[10] = (dmrid >> 0) & 0xff;
	}
	else if(protocol == "YSF"){
		out[0] = 'Y';
		out[1] = 'S';
		out[2] = 'F';
		out[3] = 'P';
		out.append(callsign);
		out.append(5, ' ');
	}
	else if(protocol == "DMR"){
		char tag[] = { 'R','P','T','P','I','N','G' };
		out.clear();
		out.append(tag, 7);
		out[7] = (dmrid >> 24) & 0xff;
		out[8] = (dmrid >> 16) & 0xff;
		out[9] = (dmrid >> 8) & 0xff;
		out[10] = (dmrid >> 0) & 0xff;
	}
	udp->writeDatagram(out, address, port);
}

void DudeStarRX::readyReadYSF()
{
	QByteArray buf;
	QByteArray out;
	QHostAddress sender;
	quint16 senderPort;
	char ysftag[11], ysfsrc[11], ysfdst[11];
	buf.resize(udp->pendingDatagramSize());
	udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
#ifdef DEBUG_YSF
	fprintf(stderr, "RECV: ");
	for(int i = 0; i < buf.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
	if(buf.size() == 14){
		if(connect_status == CONNECTING){
			ysf = new DSDYSF();
			ui->connectButton->setText("Disconnect");
			ui->connectButton->setEnabled(true);
			ui->modeCombo->setEnabled(false);
			ui->hostCombo->setEnabled(false);
			ui->callsignEdit->setEnabled(false);
			ui->comboMod->setEnabled(false);
			connect_status = CONNECTED_RW;
			ping_timer->start(5000);
		}
		status_txt->setText(" Host: " + host + ":" + QString::number(port) + " Ping: " + QString::number(ping_cnt++));
	}
	if((buf.size() == 155) && (::memcmp(buf.data(), "YSFD", 4U) == 0)){
		memcpy(ysftag, buf.data() + 4, 10);ysftag[10] = '\0';
		memcpy(ysfsrc, buf.data() + 14, 10);ysfsrc[10] = '\0';
		memcpy(ysfdst, buf.data() + 24, 10);ysfdst[10] = '\0';
		ui->mycall->setText(QString(ysftag));
		ui->urcall->setText(QString(ysfsrc));
		ui->rptr1->setText(QString(ysfdst));
		for(int i = 0; i < 115; ++i){
			ysfq.enqueue(buf.data()[40+i]);
		}
	}
}

void DudeStarRX::readyReadDMR()
{
	QByteArray buf;
	QByteArray in;
	QByteArray out;
	QHostAddress sender;
	quint16 senderPort;
	CSHA256 sha256;
	char buffer[400U];

	static bool first_tx = false;
	buf.resize(udp->pendingDatagramSize());
	udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
#ifdef DEBUG
	fprintf(stderr, "RECV: ");
	for(int i = 0; i < buf.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
	if((buf.size() == 10) && (::memcmp(buf.data(), "RPTACK", 6U) == 0)){
		switch(connect_status){
		case CONNECTING:
			connect_status = DMR_AUTH;
			in[0] = buf[6];
			in[1] = buf[7];
			in[2] = buf[8];
			in[3] = buf[9];
			in.append(dmr_password);

			out.clear();
			out.resize(40);
			out[0] = 'R';
			out[1] = 'P';
			out[2] = 'T';
			out[3] = 'K';
			out[4] = (dmrid >> 24) & 0xff;
			out[5] = (dmrid >> 16) & 0xff;
			out[6] = (dmrid >> 8) & 0xff;
			out[7] = (dmrid >> 0) & 0xff;

			sha256.buffer((unsigned char *)in.data(), (unsigned int)(dmr_password.size() + sizeof(uint32_t)), (unsigned char *)out.data() + 8U);
			break;
		case DMR_AUTH:
			out.clear();
			buffer[0] = 'R';
			buffer[1] = 'P';
			buffer[2] = 'T';
			buffer[3] = 'C';
			buffer[4] = (dmrid >> 24) & 0xff;
			buffer[5] = (dmrid >> 16) & 0xff;
			buffer[6] = (dmrid >> 8) & 0xff;
			buffer[7] = (dmrid >> 0) & 0xff;

			connect_status = DMR_CONF;
			char latitude[20U];
			::sprintf(latitude, "%08f", 50.0f);

			char longitude[20U];
			::sprintf(longitude, "%09f", 3.0f);
			::sprintf(buffer + 8U, "%-8.8s%09u%09u%02u%02u%8.8s%9.9s%03d%-20.20s%-19.19s%c%-124.124s%-40.40s%-40.40s", callsign.toStdString().c_str(),
					438800000, 438800000, 1, 1, latitude, longitude, 0, "Detroit","USA", '2', "www.dudetronics.com", "20190131", "MMDVM");
			out.append(buffer, 302);
			break;
		case DMR_CONF:
			connect_status = CONNECTED_RW;
			mbe = new MBEDecoder();
			dmr_header_timer = new QTimer();
			connect(dmr_header_timer, SIGNAL(timeout()), this, SLOT(tx_dmr_header()));
			ui->connectButton->setText("Disconnect");
			ui->connectButton->setEnabled(true);
			ui->modeCombo->setEnabled(false);
			ui->hostCombo->setEnabled(false);
			ui->callsignEdit->setEnabled(false);
			ui->dmrtgEdit->setEnabled(false);
			ping_timer->start(5000);
			tx_dmr_header();
			dmr_header_timer->start(300000);
			status_txt->setText(" Host: " + host + ":" + QString::number(port) + " Ping: " + QString::number(ping_cnt));
			break;
		default:
			break;
		}
		udp->writeDatagram(out, address, port);
	}
	if((buf.size() == 11) && (::memcmp(buf.data(), "MSTPONG", 7U) == 0)){
		status_txt->setText(" Host: " + host + ":" + QString::number(port) + " Ping: " + QString::number(ping_cnt++));
	}
	if((buf.size() == 55) && (::memcmp(buf.data(), "DMRD", 4U) == 0) && ((uint8_t)buf.data()[15] <= 0x90)){
		uint8_t dmrframe[33];
		uint8_t dmr3ambe[27];
		uint8_t dmrsync[7];
		// get the 33 bytes ambe
		memcpy(dmrframe, &(buf.data()[20]), 33);
		// extract the 3 ambe frames
		memcpy(dmr3ambe, dmrframe, 14);
		dmr3ambe[13] &= 0xF0;
		dmr3ambe[13] |= (dmrframe[19] & 0x0F);
		memcpy(&dmr3ambe[14], &dmrframe[20], 14);
		// extract sync
		dmrsync[0] = dmrframe[13] & 0x0F;
		::memcpy(&dmrsync[1], &dmrframe[14], 5);
		dmrsync[6] = dmrframe[19] & 0xF0;
		for(int i = 0; i < 27; ++i){
			audioq.enqueue(dmr3ambe[i]);
		}
		uint32_t id = (uint32_t)((buf.data()[5] << 16) | ((buf.data()[6] << 8) & 0xff00) | (buf.data()[7]) & 0xff);
		ui->mycall->setText(dmrids[id]);
		ui->urcall->setText(QString::number(id));
		ui->rptr1->setText(QString::number((uint32_t)((buf.data()[8] << 16) | ((buf.data()[9] << 8) & 0xff00) | (buf.data()[10]) & 0xff)));
		ui->rptr2->setText(QString::number((uint32_t)((buf.data()[11] << 24) | ((buf.data()[12] << 16) & 0xff0000) | ((buf.data()[13] << 8) & 0xff00) | (buf.data()[14]) & 0xff)));
		ui->streamid->setText(QString::number(buf.data()[4] & 0xff, 16));
	}

	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
}

void DudeStarRX::readyReadXLX()
{
	QByteArray buf;
	QByteArray out;
	QHostAddress sender;
	quint16 senderPort;
	buf.resize(udp->pendingDatagramSize());
	udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
#ifdef DEBUG
	fprintf(stderr, "RECV: ");
	for(int i = 0; i < buf.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
	if(buf.size() == 10){
		out.clear();
		out.resize(40);
		out[0] = 'R';
		out[1] = 'P';
		out[2] = 'T';
		out[3] = 'K';
		out[4] = (dmrid >> 24) & 0xff;
		out[5] = (dmrid >> 16) & 0xff;
		out[6] = (dmrid >> 8) & 0xff;
		out[7] = (dmrid >> 0) & 0xff;
		udp->writeDatagram(out, address, port);
	}
	else if(buf.size() == 6){
		ping_timer->start(5000);
	}
/*
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
*/
}

void DudeStarRX::readyReadXRF()
{
	QByteArray buf;
	QByteArray out;
	QHostAddress sender;
	quint16 senderPort;
	static bool sd_sync = 0;
	static int sd_seq = 0;
	char mycall[9], urcall[9], rptr1[9], rptr2[9];
	static unsigned short streamid = 0, s = 0;

	buf.resize(udp->pendingDatagramSize());
	udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
#ifdef DEBUG
	fprintf(stderr, "RECV: ");
	for(int i = 0; i < buf.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
	if ((buf.size() == 14) && (!memcmp(buf.data()+10, "ACK", 3))){
		mbe = new MBEDecoder();
		ui->connectButton->setText("Disconnect");
		ui->connectButton->setEnabled(true);
		ui->modeCombo->setEnabled(false);
		ui->hostCombo->setEnabled(false);
		ui->callsignEdit->setEnabled(false);
		ui->comboMod->setEnabled(false);
		connect_status = CONNECTED_RW;
		status_txt->setText("RW connect to " + host + ":" + QString::number(port));
	}
	if(buf.size() == 9){
		out.clear();
		out.append(callsign);
		out.append(8 - callsign.size(), ' ');
		out[8] = 0;
		udp->writeDatagram(out, address, port);
	}
	if((buf.size() == 56) && (!memcmp(buf.data(), "DSVT", 4))) {
		streamid = (buf.data()[12] << 8) | (buf.data()[13] & 0xff);
		memcpy(rptr2, buf.data() + 18, 8); rptr1[8] = '\0';
		memcpy(rptr1, buf.data() + 26, 8); rptr2[8] = '\0';
		memcpy(urcall, buf.data() + 34, 8); urcall[8] = '\0';
		memcpy(mycall, buf.data() + 42, 8); mycall[8] = '\0';
		ui->mycall->setText(QString(mycall));
		ui->urcall->setText(QString(urcall));
		ui->rptr1->setText(QString(rptr1));
		ui->rptr2->setText(QString(rptr2));
		ui->streamid->setText(QString::number(streamid, 16));	
	}
	if((buf.size() == 27) && (!memcmp(buf.data(), "DSVT", 4))) {
		s = (buf.data()[12] << 8) | (buf.data()[13] & 0xff);
		if(s != streamid){
			return;
		}
		if((buf.data()[14] == 0) && (buf.data()[24] == 0x55) && (buf.data()[25] == 0x2d) && (buf.data()[26] == 0x16)){
			sd_sync = 1;
			sd_seq = 1;
		}
		if(sd_sync && (sd_seq == 1) && (buf.data()[14] == 1) && (buf.data()[24] == 0x30)){
			user_data[0] = buf.data()[25] ^ 0x4f;
			user_data[1] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 2) && (buf.data()[14] == 2)){
			user_data[2] = buf.data()[24] ^ 0x70;
			user_data[3] = buf.data()[25] ^ 0x4f;
			user_data[4] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 3) && (buf.data()[14] == 3) && (buf.data()[24] == 0x31)){
			user_data[5] = buf.data()[25] ^ 0x4f;
			user_data[6] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 4) && (buf.data()[14] == 4)){
			user_data[7] = buf.data()[24] ^ 0x70;
			user_data[8] = buf.data()[25] ^ 0x4f;
			user_data[9] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 5) && (buf.data()[14] == 5) && (buf.data()[24] == 0x32)){
			user_data[10] = buf.data()[25] ^ 0x4f;
			user_data[11] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 6) && (buf.data()[14] == 6)){
			user_data[12] = buf.data()[24] ^ 0x70;
			user_data[13] = buf.data()[25] ^ 0x4f;
			user_data[14] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 7) && (buf.data()[14] == 7) && (buf.data()[24] == 0x33)){
			user_data[15] = buf.data()[25] ^ 0x4f;
			user_data[16] = buf.data()[26] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 8) && (buf.data()[14] == 8)){
			user_data[17] = buf.data()[24] ^ 0x70;
			user_data[18] = buf.data()[25] ^ 0x4f;
			user_data[19] = buf.data()[26] ^ 0x93;
			user_data[20] = '\0';
			sd_sync = 0;
			sd_seq = 0;
			ui->usertxt->setText(QString::fromUtf8(user_data.data()));
		}

		for(int i = 0; i < 9; ++i){
			audioq.enqueue(buf.data()[15+i]);
		}
	}
}

void DudeStarRX::readyReadDCS()
{
	QByteArray buf;
	QByteArray out;
	QHostAddress sender;
	quint16 senderPort;
	static bool sd_sync = 0;
	static int sd_seq = 0;
	char mycall[9], urcall[9], rptr1[9], rptr2[9];
	static unsigned short streamid = 0;

	buf.resize(udp->pendingDatagramSize());
	udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
#ifdef DEBUG
	fprintf(stderr, "RECV: ");
	for(int i = 0; i < buf.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
	if ((buf.size() == 14) && (!memcmp(buf.data()+10, "ACK", 3))){
		mbe = new MBEDecoder();
		ui->connectButton->setText("Disconnect");
		ui->connectButton->setEnabled(true);
		ui->modeCombo->setEnabled(false);
		ui->hostCombo->setEnabled(false);
		ui->callsignEdit->setEnabled(false);
		ui->comboMod->setEnabled(false);
		connect_status = CONNECTED_RW;
		status_txt->setText("RW connect to " + host + ":" +  QString::number(port));
	}
	if(buf.size() == 22){
		out.clear();
		out.append(callsign);
		out.append(7 - callsign.size(), ' ');
		out[7] = module;
		out[8] = 0;
		out.append(buf.data(), 8);
		out[17] = module;
		out[18] = 0x0a;
		out[19] = 0x00;
		out[20] = 0x20;
		out[21] = 0x20;
		udp->writeDatagram(out, address, port);
	}
	if((buf.size() >= 100) && (!memcmp(buf.data(), "0001", 4))) {
		streamid = (buf.data()[43] << 8) | (buf.data()[44] & 0xff);
		memcpy(rptr2, buf.data() + 7, 8); rptr1[8] = '\0';
		memcpy(rptr1, buf.data() + 15, 8); rptr2[8] = '\0';
		memcpy(urcall, buf.data() + 23, 8); urcall[8] = '\0';
		memcpy(mycall, buf.data() + 31, 8); mycall[8] = '\0';
		ui->mycall->setText(QString(mycall));
		ui->urcall->setText(QString(urcall));
		ui->rptr1->setText(QString(rptr1));
		ui->rptr2->setText(QString(rptr2));
		ui->streamid->setText(QString::number(streamid, 16));

		if((buf.data()[45] == 0) && (buf.data()[55] == 0x55) && (buf.data()[56] == 0x2d) && (buf.data()[57] == 0x16)){
			sd_sync = 1;
			sd_seq = 1;
		}
		if(sd_sync && (sd_seq == 1) && (buf.data()[45] == 1) && (buf.data()[55] == 0x30)){
			user_data[0] = buf.data()[56] ^ 0x4f;
			user_data[1] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 2) && (buf.data()[45] == 2)){
			user_data[2] = buf.data()[55] ^ 0x70;
			user_data[3] = buf.data()[56] ^ 0x4f;
			user_data[4] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 3) && (buf.data()[45] == 3) && (buf.data()[55] == 0x31)){
			user_data[5] = buf.data()[56] ^ 0x4f;
			user_data[6] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 4) && (buf.data()[45] == 4)){
			user_data[7] = buf.data()[55] ^ 0x70;
			user_data[8] = buf.data()[56] ^ 0x4f;
			user_data[9] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 5) && (buf.data()[45] == 5) && (buf.data()[55] == 0x32)){
			user_data[10] = buf.data()[56] ^ 0x4f;
			user_data[11] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 6) && (buf.data()[45] == 6)){
			user_data[12] = buf.data()[55] ^ 0x70;
			user_data[13] = buf.data()[56] ^ 0x4f;
			user_data[14] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 7) && (buf.data()[45] == 7) && (buf.data()[55] == 0x33)){
			user_data[15] = buf.data()[56] ^ 0x4f;
			user_data[16] = buf.data()[57] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 8) && (buf.data()[45] == 8)){
			user_data[17] = buf.data()[55] ^ 0x70;
			user_data[18] = buf.data()[56] ^ 0x4f;
			user_data[19] = buf.data()[57] ^ 0x93;
			user_data[20] = '\0';
			sd_sync = 0;
			sd_seq = 0;
			ui->usertxt->setText(QString::fromUtf8(user_data.data()));
		}

		for(int i = 0; i < 9; ++i){
			audioq.enqueue(buf.data()[46+i]);
		}
	}
}

void DudeStarRX::readyReadREF()
{
	QByteArray buf;
	QByteArray out;
	QHostAddress sender;
	quint16 senderPort;
	static bool sd_sync = 0;
	static int sd_seq = 0;
	char mycall[9], urcall[9], rptr1[9], rptr2[9];
	static unsigned short streamid = 0, s = 0;

	buf.resize(udp->pendingDatagramSize());
	udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);

#ifdef DEBUG
    fprintf(stderr, "RECV: ");
    for(int i = 0; i < buf.size(); ++i){
        fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
#endif
	if ((buf.size() == 5) && (buf.data()[0] == 5)){
		out[0] = 0x1c;
		out[1] = 0xc0;
		out[2] = 0x04;
		out[3] = 0x00;
		out.append(callsign.toUpper().toLocal8Bit().data(), 6);
		out[10] = 0x00;
		out[11] = 0x00;
		out[12] = 0x00;
		out[13] = 0x00;
		out[14] = 0x00;
		out[15] = 0x00;
		out[16] = 0x00;
		out[17] = 0x00;
		out[18] = 0x00;
		out[19] = 0x00;
		out.append("HS000000", 8);
		udp->writeDatagram(out, address, port);
	}
	if(buf.size() == 3){ //2 way keep alive ping
		QString s;
		if(connect_status == CONNECTED_RW){
			s = "RW";
		}
		else if(connect_status == CONNECTED_RO){
			s = "RO";
		}
		status_txt->setText(s + " Host: " + host + ":" + QString::number(port) + " Ping: " + QString::number(ping_cnt++));
		out[0] = 0x03;
		out[1] = 0x60;
		out[2] = 0x00;
		out.resize(3);
		udp->writeDatagram(out, address, port);
	}
	if((connect_status == CONNECTING) && (buf.size() == 0x08)){
		if((buf.data()[4] == 0x4f) && (buf.data()[5] == 0x4b) && (buf.data()[6] == 0x52)){ // OKRW/OKRO response
			mbe = new MBEDecoder();
			ui->connectButton->setText("Disconnect");
			ui->connectButton->setEnabled(true);
			ui->modeCombo->setEnabled(false);
			ui->hostCombo->setEnabled(false);
			ui->callsignEdit->setEnabled(false);
			//ui->comboMod->setEnabled(false);
			if(buf.data()[7] == 0x57){ //OKRW
				connect_status = CONNECTED_RW;
				status_txt->setText("RW connect to " + host);
			}
			else if(buf.data()[7] == 0x4f){ //OKRO -- Go get registered!
				connect_status = CONNECTED_RO;
				status_txt->setText("RO connect to " + host);
			}
		}
		else if((buf.data()[4] == 0x46) && (buf.data()[5] == 0x41) && (buf.data()[6] == 0x49) && (buf.data()[7] == 0x4c)){ // FAIL response
			status_txt->setText("Connection refused by " + host);
			connect_status = DISCONNECTED;
			ui->connectButton->setText("Connect");
			ui->connectButton->setEnabled(true);
		}
		else{ //Unknown response
			status_txt->setText("Unknown response by " + host);
			connect_status = DISCONNECTED;
		}
	}
#ifdef DEBUG
    if(buf.size() == 0x3a){
        std::cerr << "Module:streamid == " << (char)buf.data()[0x1b] << ":" << std::hex << (short)((buf.data()[14] << 8) | (buf.data()[15] & 0xff)) << std::endl;
    }
#endif
	if((buf.size() == 0x3a) && (!memcmp(buf.data()+1, header, 5)) ){
		memcpy(rptr2, buf.data() + 20, 8); rptr1[8] = '\0';
		memcpy(rptr1, buf.data() + 28, 8); rptr2[8] = '\0';
		memcpy(urcall, buf.data() + 36, 8); urcall[8] = '\0';
		memcpy(mycall, buf.data() + 44, 8); mycall[8] = '\0';
		module = ui->comboMod->currentText().toStdString()[0];
		QString h = hostname + " " + module;
		if( (QString(rptr2).simplified() == h.simplified()) || (QString(rptr1).simplified() == h.simplified()) ){
			streamid = (buf.data()[14] << 8) | (buf.data()[15] & 0xff);
			ui->mycall->setText(QString(mycall));
			ui->urcall->setText(QString(urcall));
			ui->rptr1->setText(QString(rptr1));
			ui->rptr2->setText(QString(rptr2));
			ui->streamid->setText(QString::number(streamid, 16));
		}
		else{
			streamid = 0;
		}
	}
	if((buf.size() == 0x1d) && (!memcmp(buf.data()+1, header, 5)) ){ //29
		//for(int i = 0; i < buf.size(); ++i){
		//	fprintf(stderr, "%02x ", (unsigned char)buf.data()[i]);
		//}
		//fprintf(stderr, "\n");
		//fflush(stderr);
		s = (buf.data()[14] << 8) | (buf.data()[15] & 0xff);
		if(s != streamid){
			return;
		}
		if((buf.data()[16] == 0) && (buf.data()[26] == 0x55) && (buf.data()[27] == 0x2d) && (buf.data()[28] == 0x16)){
			sd_sync = 1;
			sd_seq = 1;
		}
		if(sd_sync && (sd_seq == 1) && (buf.data()[16] == 1) && (buf.data()[26] == 0x30)){
			user_data[0] = buf.data()[27] ^ 0x4f;
			user_data[1] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 2) && (buf.data()[16] == 2)){
			user_data[2] = buf.data()[26] ^ 0x70;
			user_data[3] = buf.data()[27] ^ 0x4f;
			user_data[4] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 3) && (buf.data()[16] == 3) && (buf.data()[26] == 0x31)){
			user_data[5] = buf.data()[27] ^ 0x4f;
			user_data[6] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 4) && (buf.data()[16] == 4)){
			user_data[7] = buf.data()[26] ^ 0x70;
			user_data[8] = buf.data()[27] ^ 0x4f;
			user_data[9] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 5) && (buf.data()[16] == 5) && (buf.data()[26] == 0x32)){
			user_data[10] = buf.data()[27] ^ 0x4f;
			user_data[11] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 6) && (buf.data()[16] == 6)){
			user_data[12] = buf.data()[26] ^ 0x70;
			user_data[13] = buf.data()[27] ^ 0x4f;
			user_data[14] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 7) && (buf.data()[16] == 7) && (buf.data()[26] == 0x33)){
			user_data[15] = buf.data()[27] ^ 0x4f;
			user_data[16] = buf.data()[28] ^ 0x93;
			++sd_seq;
		}
		if(sd_sync && (sd_seq == 8) && (buf.data()[16] == 8)){
			user_data[17] = buf.data()[26] ^ 0x70;
			user_data[18] = buf.data()[27] ^ 0x4f;
			user_data[19] = buf.data()[28] ^ 0x93;
			user_data[20] = '\0';
			sd_sync = 0;
			sd_seq = 0;
			ui->usertxt->setText(QString::fromUtf8(user_data.data()));
		}

		for(int i = 0; i < 9; ++i){
			audioq.enqueue(buf.data()[17+i]);
		}
	}
	if(buf.size() == 0x20){ //32
		s = (buf.data()[14] << 8) | (buf.data()[15] & 0xff);
		if(s != streamid){
			return;
		}
		ui->streamid->setText("Stream complete");
		ui->usertxt->clear();
	}
}

void DudeStarRX::handleStateChanged(QAudio::State)
{
}
