#pragma once
#include "AudioEngine.h"
#include <functional>
#include <vector>

// Exportar la mezcla de una canción (un consolidado para mandar a los demás músicos): se renderiza fuera de
// tiempo real con el mismo motor que suena en vivo, así sale todo lo que se oye (arreglo, tempo y tono, faders,
// mute, nivelado, fader maestro, triggers, pistas MIDI e instrumentos VST/AU). Cada fuente va a un par de salida
// propio mientras dura el render, así también salen por separado, y el click a otro par.
namespace bounce
{
    struct Options
    {
        bool click = false;       // el click mezclado en la mezcla
        bool clickFile = false;   // el click aparte (Result::click)
        bool stems = false;       // además, cada fuente por separado (Result::stems)
    };

    struct Source
    {
        juce::String name;
        juce::AudioBuffer<float> audio;   // estéreo, del largo de la canción
    };

    struct Result
    {
        juce::AudioBuffer<float> mix;     // estéreo, del largo de la canción, a la frecuencia del motor
        juce::AudioBuffer<float> click;   // con clickFile
        std::vector<Source> stems;        // con stems: pistas, líneas de triggers y filas MIDI con banco, instrumentos
        bool aborted = false;
    };

    // Renderiza la canción `song` (la que tiene puesta `engine`) desde el inicio hasta el final. El motor NO debe
    // estar conectado a un dispositivo mientras tanto (nadie más llama a su callback). Ignora el solo (respeta el
    // mute) y quita el loop; al terminar devuelve las salidas, los solos y el click como estaban y deja el motor
    // detenido (la posición y el loop los repone quien llama). Los instrumentos se procesan en modo no tiempo real.
    Result render (AudioEngine& engine, LoadedSong& song, const Options& options,
                   const std::function<bool()>& shouldAbort = {}, const std::function<void (float)>& progress = {});

    // Lleva la mezcla a esa sonoridad (EBU R128) con la misma ganancia y el mismo limitador de picos (techo de -1,5
    // dB, como el nivelado del mix) en las pistas, así siguen sumando la mezcla; el click no se toca. Devuelve la
    // ganancia aplicada en dB (0 si la mezcla está en silencio).
    double normalize (Result&, double sampleRate, double targetLufs);
    // Si algún pico de la mezcla o de una pista pasa de 0,999, baja todo por igual (devuelve los dB que bajó)
    double preventClipping (Result&);
}
