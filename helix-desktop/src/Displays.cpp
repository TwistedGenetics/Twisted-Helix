#include "Displays.h"
#include "helix/Envelope.h"
#include "helix/FFT.h"
#include "helix/ModSystem.h"
#include "helix/Oscillator.h"
#include <complex>

using namespace helix;

void drawDisplayBackground(juce::Graphics& g, juce::Rectangle<float> r)
{
    const Theme& t = theme();
    g.setColour(t.display);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(t.displayGrid);
    for (int i = 1; i < 8; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float)i / 8.0f;
        g.drawVerticalLine((int)x, r.getY() + 2.0f, r.getBottom() - 2.0f);
    }
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine((int)(r.getY() + r.getHeight() * (float)i / 4.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
    g.setColour(t.edge);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

// ------------------------------------------------------------------------------------------- wavetable

WavetableView::WavetableView(HelixProcessor& p, int slot) : proc_(p), slot_(slot)
{
    threeD_ = proc_.settings().getBoolValue("wt3d", true);
}

float WavetableView::position() const
{
    const int pos = slot_ == 0 ? P_A_POS : P_B_POS;
    return proc_.engine().activeVoices() > 0 ? proc_.engine().liveNormalised(pos) : proc_.parameter(pos)->getValue();
}

void WavetableView::refresh()
{
    auto table = proc_.engine().uiTable(slot_);
    const int o = slot_ == 0 ? 0 : P_B_ON - P_A_ON;
    const float w[4] = {proc_.engine().params().get(P_A_W1_MODE + o), proc_.engine().params().get(P_A_W1_AMT + o),
                        proc_.engine().params().get(P_A_W2_MODE + o), proc_.engine().params().get(P_A_W2_AMT + o)};
    const float pos = position();
    bool changed = table.get() != lastTable_ || std::abs(pos - lastPos_) > 1e-4f;
    for (int i = 0; i < 4; ++i) changed |= w[i] != lastW_[i];
    if (!changed) return;
    lastTable_ = table.get();
    lastPos_ = pos;
    std::copy(w, w + 4, lastW_);
    repaint();
}

void WavetableView::mouseUp(const juce::MouseEvent& e)
{
    if (modeTag_.contains(e.getPosition()))
    {
        threeD_ = !threeD_;
        proc_.settings().setValue("wt3d", threeD_);
        repaint();
    }
}

void WavetableView::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto area = getLocalBounds().toFloat();
    drawDisplayBackground(g, area);
    auto table = proc_.engine().uiTable(slot_);
    if (!table) return;

    const int nf = table->numFrames();
    const float posN = juce::jlimit(0.0f, 1.0f, position());
    const float fpos = posN * (float)(nf - 1);
    const int o = slot_ == 0 ? 0 : P_B_ON - P_A_ON;
    const Warp w1 = (Warp)proc_.engine().params().getInt(P_A_W1_MODE + o);
    const Warp w2 = (Warp)proc_.engine().params().getInt(P_A_W2_MODE + o);
    const float a1 = proc_.engine().params().get(P_A_W1_AMT + o), a2 = proc_.engine().params().get(P_A_W2_AMT + o);

    // sample of frame f (fractional frames blend), optionally through the phase warps
    auto sampleAt = [&](float frame, float ph, bool warp) {
        float q = ph;
        if (warp)
        {
            if (w1 != Warp::Off) q = WtOscillator::warpPhase(w1, q, a1, 0.0f);
            if (w2 != Warp::Off) q = WtOscillator::warpPhase(w2, q, a2, 0.0f);
            q = juce::jmin(q, 0.9985f);
            q -= std::floor(q);
        }
        const int f0 = juce::jlimit(0, nf - 1, (int)frame), f1 = juce::jmin(nf - 1, f0 + 1);
        const float ft = frame - (float)f0;
        const float x = q * (float)kFrameSize;
        const int i = (int)x & (kFrameSize - 1);
        const float fr = x - std::floor(x);
        const float* A = table->fullFrame(f0);
        const float* B = table->fullFrame(f1);
        const float sa = A[i] + (A[i + 1] - A[i]) * fr, sb = B[i] + (B[i + 1] - B[i]) * fr;
        return sa + (sb - sa) * ft;
    };

    const int points = 110;
    auto r = area.reduced(10.0f, 22.0f).withTrimmedBottom(-8.0f);
    // Only phase-shaping warps can be pictured from the table alone (FM / AM / RM need the other source).
    auto drawable = [](Warp w) { return w != Warp::Off && w != Warp::FmOther && w != Warp::FmSub && w != Warp::FmNoise &&
                                        w != Warp::Am && w != Warp::Rm; };
    const bool warped = drawable(w1) || drawable(w2);

    if (threeD_ && nf > 1)
    {
        // Perspective stack: frame 0 at the front (bottom left), the last frame at the back (top right).
        auto frameGeom = [&](float depth, float& x0, float& base, float& width, float& amp) {
            const float sc = 1.0f - 0.22f * depth;
            width = r.getWidth() * 0.70f * sc;
            amp = r.getHeight() * 0.21f * sc;
            x0 = r.getX() + r.getWidth() * 0.015f + depth * r.getWidth() * 0.28f;
            base = r.getBottom() - r.getHeight() * 0.22f - depth * r.getHeight() * 0.50f;
        };
        auto curve = [&](float frame, float depth, bool warp, juce::Path& line, juce::Path& fill) {
            float x0, base, width, amp;
            frameGeom(depth, x0, base, width, amp);
            fill.startNewSubPath(x0, base);
            for (int i = 0; i <= points; ++i)
            {
                const float ph = (float)i / (float)points;
                const float x = x0 + width * ph, y = base - sampleAt(frame, ph * 0.9985f, warp) * amp;
                if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
                fill.lineTo(x, y);
            }
            fill.lineTo(x0 + width, base);
            fill.closeSubPath();
        };
        // floor rails
        {
            float x0f, bf, wf, af, x0b, bb, wb, ab;
            frameGeom(0.0f, x0f, bf, wf, af);
            frameGeom(1.0f, x0b, bb, wb, ab);
            g.setColour(t.displayGrid.brighter(t.dark ? 0.25f : -0.05f));
            g.drawLine(x0f, bf, x0b, bb, 1.0f);
            g.drawLine(x0f + wf, bf, x0b + wb, bb, 1.0f);
        }
        const int shown = juce::jmin(nf, 44);
        const float cur = fpos;
        bool curDrawn = false;
        for (int k = shown - 1; k >= 0; --k)
        {
            const float frame = shown > 1 ? (float)k * (float)(nf - 1) / (float)(shown - 1) : 0.0f;
            const float depth = frame / (float)(nf - 1);
            // the current frame is drawn in its place in the stack, so frames in front of it still hide it
            if (!curDrawn && frame < cur)
            {
                curDrawn = true;
                juce::Path line, fill;
                curve(cur, cur / (float)(nf - 1), warped, line, fill);
                g.setColour(t.display.withAlpha(0.92f));
                g.fillPath(fill);
                g.setGradientFill(juce::ColourGradient(t.accent.withAlpha(0.35f), 0.0f, fill.getBounds().getY(),
                                                       t.accent.withAlpha(0.02f), 0.0f, fill.getBounds().getBottom(), false));
                g.fillPath(fill);
                g.setColour(t.accent.withAlpha(0.14f));
                g.strokePath(line, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(t.accent.withAlpha(0.30f));
                g.strokePath(line, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(t.accent.brighter(0.15f));
                g.strokePath(line, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            juce::Path line, fill;
            curve(frame, depth, false, line, fill);
            g.setColour(t.display.withAlpha(t.dark ? 0.86f : 0.80f));
            g.fillPath(fill);
            const float near = 1.0f - depth;
            g.setColour(t.waveBack.interpolatedWith(t.text, t.dark ? 0.15f * near : 0.0f).withAlpha(0.30f + 0.55f * near));
            g.strokePath(line, juce::PathStrokeType(0.9f + 0.5f * near));
        }
        if (!curDrawn)
        {
            juce::Path line, fill;
            curve(cur, cur / (float)(nf - 1), warped, line, fill);
            g.setColour(t.accent.withAlpha(0.3f));
            g.strokePath(line, juce::PathStrokeType(4.5f));
            g.setColour(t.accent);
            g.strokePath(line, juce::PathStrokeType(2.0f));
        }
    }
    else
    {
        // 2D: the current frame large, neighbours faint
        const float mid = r.getCentreY(), amp = r.getHeight() * 0.42f;
        auto draw2d = [&](float frame, bool warp, juce::Colour c, float thick, bool filled) {
            juce::Path line, fill;
            fill.startNewSubPath(r.getX(), mid);
            for (int i = 0; i <= points * 2; ++i)
            {
                const float ph = (float)i / (float)(points * 2);
                const float x = r.getX() + r.getWidth() * ph, y = mid - sampleAt(frame, ph * 0.9985f, warp) * amp;
                if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
                fill.lineTo(x, y);
            }
            fill.lineTo(r.getRight(), mid);
            fill.closeSubPath();
            if (filled)
            {
                g.setGradientFill(juce::ColourGradient(c.withAlpha(0.30f), 0, r.getY(), c.withAlpha(0.0f), 0, mid, false));
                g.fillPath(fill);
            }
            g.setColour(c);
            g.strokePath(line, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        g.setColour(t.displayGrid.brighter(0.2f));
        g.drawHorizontalLine((int)mid, r.getX(), r.getRight());
        for (int d : {-3, -2, -1, 1, 2, 3})
        {
            const float f = fpos + (float)d * juce::jmax(1.0f, (float)nf / 24.0f);
            if (f >= 0.0f && f <= (float)(nf - 1)) draw2d(f, false, t.waveBack.withAlpha(0.45f - 0.1f * (float)std::abs(d)), 1.0f, false);
        }
        g.setColour(t.accent.withAlpha(0.15f));
        draw2d(fpos, warped, t.accent, 2.2f, true);
    }

    // labels
    g.setColour(t.text);
    g.setFont(ui::font(12.0f, true));
    g.drawText(juce::String(table->name()), area.reduced(10.0f, 5.0f).removeFromTop(16.0f), juce::Justification::centredLeft);
    g.setColour(t.muted);
    g.setFont(ui::font(11.0f));
    g.drawText("frame " + juce::String(juce::roundToInt(fpos) + 1) + " / " + juce::String(nf) + (warped ? "  -  warped" : ""),
               area.reduced(10.0f, 5.0f).removeFromBottom(14.0f), juce::Justification::centredLeft);
    modeTag_ = area.reduced(8.0f, 5.0f).removeFromTop(16.0f).removeFromRight(30.0f).toNearestInt();
    g.setColour(t.panel2);
    g.fillRoundedRectangle(modeTag_.toFloat(), 3.0f);
    g.setColour(t.mod);
    g.setFont(ui::font(10.5f, true));
    g.drawText(threeD_ ? "3D" : "2D", modeTag_, juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------- filter response

void FilterResponseView::refresh()
{
    const int o = filter_ == 0 ? 0 : P_F2_ON - P_F1_ON;
    const auto& p = proc_.engine().params();
    const int cutP = P_F1_CUTOFF + o;
    const float cut = proc_.engine().activeVoices() > 0 ? paramDef(cutP).toPlain(proc_.engine().liveNormalised(cutP)) : p.get(cutP);
    const float key[6] = {p.get(P_F1_ON + o), p.get(P_F1_TYPE + o), cut, p.get(P_F1_RES + o), p.get(P_F1_DRIVE + o), p.get(P_F1_FAT + o)};
    if (std::equal(key, key + 6, key_)) return;
    std::copy(key, key + 6, key_);
    compute();
    repaint();
}

void FilterResponseView::compute()
{
    const int N = 4096;
    const float sr = 44100.0f;
    Filter f;
    f.prepare(sr);
    f.setType((FilterType)(int)key_[1]);
    f.update(key_[2], key_[3], key_[4], key_[5]);
    std::vector<std::complex<double>> x((size_t)N);
    const float imp = 0.05f;   // small, so drive and fat stay near their linear region
    for (int i = 0; i < N; ++i)
    {
        float l = i == 0 ? imp : 0.0f, r = l;
        f.process(l, r);
        x[(size_t)i] = std::complex<double>(l / imp, 0.0);
    }
    fft(x, false);
    db_.assign(220, 0.0f);
    for (int k = 0; k < 220; ++k)
    {
        const double hz = 20.0 * std::pow(1000.0, (double)k / 219.0);
        const double bin = hz * N / sr;
        const int b0 = (int)bin;
        const double fr = bin - b0;
        const double m = std::abs(x[(size_t)b0]) * (1.0 - fr) + std::abs(x[(size_t)juce::jmin(N / 2, b0 + 1)]) * fr;
        db_[(size_t)k] = (float)(20.0 * std::log10(std::max(m, 1e-6)));
    }
}

void FilterResponseView::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto area = getLocalBounds().toFloat();
    drawDisplayBackground(g, area);
    auto r = area.reduced(6.0f, 8.0f);
    const float top = 24.0f, bottom = -36.0f;
    auto yOf = [&](float db) { return r.getY() + (top - juce::jlimit(bottom, top, db)) / (top - bottom) * r.getHeight(); };
    g.setColour(t.displayGrid.brighter(0.25f));
    g.drawHorizontalLine((int)yOf(0.0f), r.getX(), r.getRight());
    if (db_.empty()) return;
    const bool on = key_[0] >= 0.5f;
    juce::Path line, fill;
    fill.startNewSubPath(r.getX(), r.getBottom());
    for (size_t k = 0; k < db_.size(); ++k)
    {
        const float x = r.getX() + r.getWidth() * (float)k / (float)(db_.size() - 1);
        const float y = on ? yOf(db_[k]) : yOf(0.0f);
        if (k == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
        fill.lineTo(x, y);
    }
    fill.lineTo(r.getRight(), r.getBottom());
    fill.closeSubPath();
    const juce::Colour c = on ? t.accent : t.muted;
    g.setGradientFill(juce::ColourGradient(c.withAlpha(0.28f), 0, r.getY(), c.withAlpha(0.02f), 0, r.getBottom(), false));
    g.fillPath(fill);
    g.setColour(c);
    g.strokePath(line, juce::PathStrokeType(1.8f));
    g.setColour(t.muted);
    g.setFont(ui::font(9.5f));
    for (auto [hz, label] : {std::pair<float, const char*>{100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}})
    {
        const float x = r.getX() + r.getWidth() * std::log(hz / 20.0f) / std::log(1000.0f);
        g.drawText(label, juce::Rectangle<float>(x - 15.0f, r.getBottom() - 11.0f, 30.0f, 11.0f), juce::Justification::centred);
    }
}

// ------------------------------------------------------------------------------------------- envelope

void EnvelopeView::refresh()
{
    const int b = P_E1_A + env_ * (P_E2_A - P_E1_A);
    float key[7];
    for (int i = 0; i < 7; ++i) key[i] = proc_.engine().params().get(b + i);
    if (std::equal(key, key + 7, key_) && !curve_.empty()) return;
    std::copy(key, key + 7, key_);
    // Run the engine's own envelope: what you see is exactly what you hear.
    const float a = key[0], d = key[1], s = key[2], r = key[3];
    const float hold = juce::jmax(80.0f, 0.35f * (a + d + r));
    const float totalMs = a + d + hold + r * 1.3f;
    const float sr = 900.0f / (totalMs / 1000.0f);
    Envelope e;
    e.setSampleRate(sr);
    e.set(a, d, s, r, key[4], key[5], key[6]);
    e.gate(true);
    curve_.clear();
    const int gateOff = (int)((a + d + hold) * 0.001f * sr);
    for (int i = 0; i < 900; ++i)
    {
        if (i == gateOff) { e.gate(false); releaseAt_ = i; }
        curve_.push_back(e.process());
    }
    repaint();
}

void EnvelopeView::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto area = getLocalBounds().toFloat();
    drawDisplayBackground(g, area);
    if (curve_.empty()) return;
    auto r = area.reduced(8.0f, 12.0f).withTrimmedTop(12.0f);
    juce::Path line, fill;
    fill.startNewSubPath(r.getX(), r.getBottom());
    const int n = (int)curve_.size();
    for (int i = 0; i < n; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float)i / (float)(n - 1), y = r.getBottom() - curve_[(size_t)i] * r.getHeight();
        if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
        fill.lineTo(x, y);
    }
    fill.lineTo(r.getRight(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(t.mod.withAlpha(0.30f), 0, r.getY(), t.mod.withAlpha(0.03f), 0, r.getBottom(), false));
    g.fillPath(fill);
    g.setColour(t.mod);
    g.strokePath(line, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float rx = r.getX() + r.getWidth() * (float)releaseAt_ / (float)(n - 1);
    g.setColour(t.muted.withAlpha(0.6f));
    g.drawVerticalLine((int)rx, r.getY(), r.getBottom());
    g.setFont(ui::font(10.0f));
    g.drawText("key up", juce::Rectangle<float>(rx + 3.0f, r.getY(), 60.0f, 12.0f), juce::Justification::centredLeft);
    g.setColour(t.text);
    g.setFont(ui::font(11.5f, true));
    g.drawText(env_ == 0 ? "ENV 1  (AMP)" : "ENV " + juce::String(env_ + 1), area.reduced(8.0f, 4.0f).removeFromTop(14.0f), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------------- LFO

float LfoView::valueAt(float ph) const
{
    const int b = P_L1_SHAPE + lfo_ * (P_L2_SHAPE - P_L1_SHAPE);
    const auto shape = (LfoShape)proc_.engine().params().getInt(b);
    const auto& sh = proc_.engine().lfoShapes();
    auto rnd = [](int k) { uint32_t s = (uint32_t)k * 2654435761u + 977u; s ^= s >> 13; s *= 0x5bd1e995u; s ^= s >> 15; return (float)(s & 0xffff) / 32767.5f - 1.0f; };
    switch (shape)
    {
        case LfoShape::Sine: return std::sin(juce::MathConstants<float>::twoPi * ph);
        case LfoShape::Triangle: return ph < 0.25f ? 4 * ph : (ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4);
        case LfoShape::SawUp: return 2.0f * ph - 1.0f;
        case LfoShape::SawDown: return 1.0f - 2.0f * ph;
        case LfoShape::Square: return ph < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleHold: return rnd((int)(ph * 8.0f));
        case LfoShape::SmoothRandom:
        {
            const int k = (int)(ph * 4.0f);
            const float tt = ph * 4.0f - (float)k, c = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * tt);
            return rnd(k) + (rnd(k + 1) - rnd(k)) * c;
        }
        case LfoShape::Breakpoint: return sh.evalBreakpoint(lfo_, ph);
        case LfoShape::Steps:
        {
            const int n = juce::jlimit(1, kMaxSteps, proc_.engine().params().getInt(b + 8));
            return sh.steps[lfo_][juce::jmin(n - 1, (int)(ph * (float)n))].load();
        }
    }
    return 0.0f;
}

void LfoView::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto area = getLocalBounds().toFloat();
    drawDisplayBackground(g, area);
    auto r = plot();
    const float mid = r.getCentreY();
    g.setColour(t.displayGrid.brighter(0.25f));
    g.drawHorizontalLine((int)mid, r.getX(), r.getRight());
    juce::Path line;
    const int n = 300;
    for (int i = 0; i <= n; ++i)
    {
        const float ph = (float)i / (float)n;
        const float x = r.getX() + r.getWidth() * ph, y = mid - valueAt(juce::jmin(ph, 0.99999f)) * r.getHeight() * 0.5f;
        if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
    }
    g.setColour(t.mod.withAlpha(0.18f));
    g.strokePath(line, juce::PathStrokeType(6.0f));
    g.setColour(t.mod);
    g.strokePath(line, juce::PathStrokeType(2.0f));
    const int b = P_L1_SHAPE + lfo_ * (P_L2_SHAPE - P_L1_SHAPE);
    const auto shape = (LfoShape)proc_.engine().params().getInt(b);
    if (shape == LfoShape::Breakpoint)
    {
        const auto& sh = proc_.engine().lfoShapes();
        const int np = sh.numPoints[lfo_].load();
        for (int k = 0; k < np; ++k)
        {
            const float x = r.getX() + r.getWidth() * sh.px[lfo_][k].load(), y = mid - sh.py[lfo_][k].load() * r.getHeight() * 0.5f;
            g.setColour(t.text);
            g.fillEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        }
    }
    // start phase marker
    const float phase = proc_.engine().params().get(b + 7);
    g.setColour(t.accent.withAlpha(0.7f));
    g.drawVerticalLine((int)(r.getX() + r.getWidth() * phase), r.getY(), r.getBottom());
    g.setColour(t.muted);
    g.setFont(ui::font(10.0f));
    const juce::String hint = shape == LfoShape::Steps ? "drag to draw steps"
                            : shape == LfoShape::Breakpoint ? "drag points, double-click adds, right-click removes" : "";
    g.drawText(hint, area.reduced(8.0f, 3.0f).removeFromBottom(12.0f), juce::Justification::centredRight);
}

void LfoView::editSteps(const juce::MouseEvent& e)
{
    const int b = P_L1_SHAPE + lfo_ * (P_L2_SHAPE - P_L1_SHAPE);
    const int n = juce::jlimit(1, kMaxSteps, proc_.engine().params().getInt(b + 8));
    auto r = plot();
    const int k = juce::jlimit(0, n - 1, (int)((e.position.x - r.getX()) / r.getWidth() * (float)n));
    const float v = juce::jlimit(-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f));
    proc_.engine().lfoShapes().steps[lfo_][k].store(v);
    repaint();
}

void LfoView::mouseDown(const juce::MouseEvent& e)
{
    const int b = P_L1_SHAPE + lfo_ * (P_L2_SHAPE - P_L1_SHAPE);
    const auto shape = (LfoShape)proc_.engine().params().getInt(b);
    if (shape == LfoShape::Steps) { editSteps(e); return; }
    if (shape != LfoShape::Breakpoint) return;
    auto& sh = proc_.engine().lfoShapes();
    auto r = plot();
    const int np = sh.numPoints[lfo_].load();
    dragPoint_ = -1;
    for (int k = 0; k < np; ++k)
    {
        const float x = r.getX() + r.getWidth() * sh.px[lfo_][k].load(), y = r.getCentreY() - sh.py[lfo_][k].load() * r.getHeight() * 0.5f;
        if (e.position.getDistanceFrom({x, y}) < 9.0f) dragPoint_ = k;
    }
    if (dragPoint_ >= 0 && e.mods.isPopupMenu() && np > 2)
    {
        for (int k = dragPoint_; k < np - 1; ++k) { sh.px[lfo_][k].store(sh.px[lfo_][k + 1].load()); sh.py[lfo_][k].store(sh.py[lfo_][k + 1].load()); }
        sh.numPoints[lfo_].store(np - 1);
        dragPoint_ = -1;
        repaint();
    }
}

void LfoView::mouseDrag(const juce::MouseEvent& e)
{
    const int b = P_L1_SHAPE + lfo_ * (P_L2_SHAPE - P_L1_SHAPE);
    const auto shape = (LfoShape)proc_.engine().params().getInt(b);
    if (shape == LfoShape::Steps) { editSteps(e); return; }
    if (dragPoint_ < 0) return;
    auto& sh = proc_.engine().lfoShapes();
    auto r = plot();
    const int np = sh.numPoints[lfo_].load();
    const float lo = dragPoint_ > 0 ? sh.px[lfo_][dragPoint_ - 1].load() : 0.0f;
    const float hi = dragPoint_ < np - 1 ? sh.px[lfo_][dragPoint_ + 1].load() : 1.0f;
    float x = juce::jlimit(lo, hi, (e.position.x - r.getX()) / r.getWidth());
    if (dragPoint_ == 0) x = 0.0f;
    if (dragPoint_ == np - 1) x = 1.0f;
    sh.px[lfo_][dragPoint_].store(x);
    sh.py[lfo_][dragPoint_].store(juce::jlimit(-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f)));
    repaint();
}

void LfoView::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int b = P_L1_SHAPE + lfo_ * (P_L2_SHAPE - P_L1_SHAPE);
    if ((LfoShape)proc_.engine().params().getInt(b) != LfoShape::Breakpoint) return;
    auto& sh = proc_.engine().lfoShapes();
    const int np = sh.numPoints[lfo_].load();
    if (np >= kLfoMaxPoints) return;
    auto r = plot();
    const float x = juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
    const float y = juce::jlimit(-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f));
    int at = np;
    for (int k = 0; k < np; ++k) if (sh.px[lfo_][k].load() > x) { at = k; break; }
    if (at == 0) at = 1;
    for (int k = np; k > at; --k) { sh.px[lfo_][k].store(sh.px[lfo_][k - 1].load()); sh.py[lfo_][k].store(sh.py[lfo_][k - 1].load()); }
    sh.px[lfo_][at].store(x);
    sh.py[lfo_][at].store(y);
    sh.numPoints[lfo_].store(np + 1);
    repaint();
}

// ------------------------------------------------------------------------------------------- performer

void PerformerView::paint(juce::Graphics& g)
{
    const Theme& t = theme();
    auto area = getLocalBounds().toFloat();
    drawDisplayBackground(g, area);
    auto r = plot();
    const int b = perf_ == 0 ? P_P1_DIV : P_P2_DIV;
    float snap[kNumParams];
    for (int i = 0; i < kNumParams; ++i) snap[i] = proc_.engine().params().get(i);
    const int len = juce::jlimit(1, kMaxSteps, (int)snap[b + 1]);
    const float mid = r.getCentreY(), cw = r.getWidth() / (float)len;
    for (int k = 0; k < len; ++k)
    {
        const float v = snap[b + 3 + k];
        auto bar = juce::Rectangle<float>(r.getX() + cw * (float)k + 1.5f, juce::jmin(mid, mid - v * r.getHeight() * 0.5f),
                                          cw - 3.0f, std::abs(v) * r.getHeight() * 0.5f);
        g.setColour(t.mod.withAlpha(0.18f));
        g.fillRect(bar);
    }
    juce::Path line;
    const int n = len * 24;
    for (int i = 0; i <= n; ++i)
    {
        const double pos = (double)i / 24.0;
        const float v = performerValue(snap, perf_, juce::jmin(pos, (double)len - 1e-4));
        const float x = r.getX() + r.getWidth() * (float)i / (float)n, y = mid - v * r.getHeight() * 0.5f;
        if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
    }
    g.setColour(t.mod);
    g.strokePath(line, juce::PathStrokeType(2.0f));
    g.setColour(t.displayGrid.brighter(0.25f));
    g.drawHorizontalLine((int)mid, r.getX(), r.getRight());
    g.setColour(t.muted);
    g.setFont(ui::font(10.0f));
    g.drawText("drag to set steps, right-click a step for its shape", area.reduced(8.0f, 3.0f).removeFromBottom(12.0f),
               juce::Justification::centredRight);
}

void PerformerView::setStep(const juce::MouseEvent& e)
{
    const int b = perf_ == 0 ? P_P1_DIV : P_P2_DIV;
    const int len = juce::jlimit(1, kMaxSteps, proc_.engine().params().getInt(b + 1));
    auto r = plot();
    const int k = juce::jlimit(0, len - 1, (int)((e.position.x - r.getX()) / r.getWidth() * (float)len));
    const float v = juce::jlimit(-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f));
    proc_.setParamFromUI(b + 3 + k, v);
    repaint();
}

void PerformerView::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu()) { setStep(e); return; }
    const int b = perf_ == 0 ? P_P1_DIV : P_P2_DIV;
    const int len = juce::jlimit(1, kMaxSteps, proc_.engine().params().getInt(b + 1));
    auto r = plot();
    const int k = juce::jlimit(0, len - 1, (int)((e.position.x - r.getX()) / r.getWidth() * (float)len));
    juce::PopupMenu m;
    m.addSectionHeader("Step " + juce::String(k + 1) + " shape");
    const int cur = proc_.engine().params().getInt(b + 19 + k);
    for (int s = 0; s < (int)choices::perfStepShape.size(); ++s) m.addItem(s + 1, choices::perfStepShape[(size_t)s], true, s == cur);
    juce::Component::SafePointer<PerformerView> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [self, b, k](int res) {
        if (self && res > 0) { self->proc_.setParamFromUI(b + 19 + k, (float)(res - 1)); self->repaint(); }
    });
}

void PerformerView::mouseDrag(const juce::MouseEvent& e) { if (!e.mods.isPopupMenu()) setStep(e); }
