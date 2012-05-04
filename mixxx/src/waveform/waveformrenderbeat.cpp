
#include <QDebug>
#include <QColor>
#include <QDomNode>
#include <QPaintEvent>
#include <QPainter>
#include <QObject>
#include <QVector>

#include "waveformrenderbeat.h"
#include "waveformrenderer.h"
#include "controlobjectthreadmain.h"
#include "controlobject.h"
#include "widget/wskincolor.h"
#include "widget/wwidget.h"
#include "trackinfoobject.h"
#include "track/beats.h"


WaveformRenderBeat::WaveformRenderBeat(const char* group, WaveformRenderer *parent)
        : m_pParent(parent),
          m_pTrackSamples(NULL),
          m_pTrack(),
          m_iWidth(0),
          m_iHeight(0),
          m_dSamplesPerPixel(-1),
          m_dSamplesPerDownsample(-1),
          m_iNumSamples(0),
          m_iSampleRate(0),
          m_bBeatActive(false) {
    m_pTrackSamples = new ControlObjectThreadMain(
        ControlObject::getControl(ConfigKey(group,"track_samples")));
    slotUpdateTrackSamples(m_pTrackSamples->get());
    connect(m_pTrackSamples, SIGNAL(valueChanged(double)),
            this, SLOT(slotUpdateTrackSamples(double)));

    m_pTrackSampleRate = new ControlObjectThreadMain(
        ControlObject::getControl(ConfigKey(group,"track_samplerate")));
    slotUpdateTrackSampleRate(m_pTrackSampleRate->get());
    connect(m_pTrackSampleRate, SIGNAL(valueChanged(double)),
            this, SLOT(slotUpdateTrackSampleRate(double)));

    m_pBeatActive = new ControlObjectThreadMain(
        ControlObject::getControl(ConfigKey(group,"beat_active")));
    slotUpdateBeatActive(m_pBeatActive->get());
    connect(m_pBeatActive, SIGNAL(valueChanged(double)),
            this, SLOT(slotUpdateBeatActive(double)));
}

WaveformRenderBeat::~WaveformRenderBeat() {
   delete m_pTrackSamples;
   delete m_pBeatActive;
    qDebug() << this << "~WaveformRenderBeat()";
}

void WaveformRenderBeat::slotUpdateTrackSamples(double samples) {
    //qDebug() << "WaveformRenderBeat :: samples = " << int(samples);
    m_iNumSamples = static_cast<int>(samples);
}

void WaveformRenderBeat::slotUpdateTrackSampleRate(double sampleRate) {


    // f = z * m * n
    double m = m_pParent->getSubpixelsPerPixel();
    double f = sampleRate;
    double z = m_pParent->getPixelsPerSecond();
    double n = f / (m*z);

    m_iSampleRate = static_cast<int>(sampleRate);
    m_dSamplesPerDownsample = n;
    m_dSamplesPerPixel = f/z;

    //qDebug() << "WaveformRenderBeat :: sampleRate = " << int(sampleRate)
    //         << "samplesPerDownsample" << m_dSamplesPerDownsample
    //         << "samplesPerPixel" << m_dSamplesPerPixel;
}

void WaveformRenderBeat::slotUpdateBeatActive(double beatActive) {
    m_bBeatActive = beatActive > 0;
}

void WaveformRenderBeat::resize(int w, int h) {
    m_iWidth = w;
    m_iHeight = h;
}

void WaveformRenderBeat::newTrack(TrackPointer pTrack) {
    m_pTrack = pTrack;
}

void WaveformRenderBeat::setup(QDomNode node) {
    colorMarks.setNamedColor(WWidget::selectNodeQString(node, "BeatColor"));
    colorMarks = WSkinColor::getCorrectColor(colorMarks);

    colorHighlight = Qt::black;
    QString highlight = WWidget::selectNodeQString(node, "BeatHighlightColor");
    if (highlight != "") {
        colorHighlight.setNamedColor(highlight);
    }
    colorHighlight = WSkinColor::getCorrectColor(colorHighlight);
}

void WaveformRenderBeat::draw(QPainter *pPainter, QPaintEvent *event,
                              QVector<float> *buffer, double dPlayPos, double rateAdjust) {
    slotUpdateTrackSamples(m_pTrackSamples->get());

    if(m_iSampleRate <= 0 || m_iNumSamples == 0)
        return;

    if(buffer == NULL)
        return;

    BeatsPointer pBeats = m_pTrack->getBeats();
    if (!pBeats)
        return;

    int iCurPos = (int)(dPlayPos * m_iNumSamples);
    if(iCurPos % 2 != 0)
        iCurPos--;

    // iCurPos is the current sample being processed the current pixel
    // p, with respect to iCurPos is in the screen if it is less than
    // halfw from iCurPos. A sample is a beat if it satisifes the following:

    // for b beats per minute, that means b/60 beats per seconds, or 60/b seconds per beat.
    // with a sample rate of f (generally 44khz),
    //   60f/b = samples per beat

    // Therefore, sample s is a beat if it satisfies  s % 60f/b == 0.
    // where s is a /mono/ sample

    double subpixelsPerPixel = m_pParent->getSubpixelsPerPixel()*(1.0+rateAdjust);

    QPen marksPen(colorMarks);
    marksPen.setWidth(subpixelsPerPixel*1.5);
    QPen highlightPen(colorHighlight);
    highlightPen.setWidth(subpixelsPerPixel*1.5);

    pPainter->save();
    pPainter->scale(1.0/subpixelsPerPixel,1.0);

    pPainter->setPen(marksPen);

    double subpixelWidth = m_iWidth * subpixelsPerPixel;
    double subpixelHalfWidth = subpixelWidth / 2.0;
    double halfh = m_iHeight/2;

    // basePos and endPos are in samples
    double basePos = iCurPos - m_dSamplesPerPixel * m_iWidth * (1.0+rateAdjust);
    double endPos = basePos + (2 * m_iWidth) * m_dSamplesPerPixel * (1.0+rateAdjust);


    m_beatList.clear();
    pBeats->findBeats(basePos, endPos, &m_beatList);

    bool reset = false;
    foreach (double curPos, m_beatList) {
        if (curPos < 0)
            continue;

        // i relative to the current play position in subpixels
        double i = (((curPos) - iCurPos)/2)/m_dSamplesPerDownsample;

        // If a beat is active, highlight the marker.
        if(m_bBeatActive && abs(i) < 20) {
            pPainter->setPen(highlightPen);
            reset = true;
        }

        // Translate from -subpixelHalfWidth..subpixelHalfwidth to 0..subpixelWidth
        i += subpixelHalfWidth;

        pPainter->drawLine(QLineF(i,halfh,i,-halfh));

        if(reset) {
            pPainter->setPen(marksPen);
            reset = false;
        }
    }

    pPainter->restore();
}