#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MixProject.h"
#include <functional>
#include <vector>

// Sección "Armar mix": ocupa el área de la canción (a la derecha del setlist) mientras se arma un
// mix. Muestra y edita un MixProject que no es suyo (el dueño es MainComponent, que hace todo lo
// pesado: copiar fuentes, analizar, calcular formas de onda, renderizar, reproducir, crear la
// canción). Las ediciones de tramos, tempo y análisis se hacen aquí directamente sobre el proyecto
// en el hilo de mensajes y se avisan con onChanged; lo demás se pide con los demás callbacks.
//
// Partes: cabecera (nombre, tempo del mix, "Cada tramo a su tempo", Crear canción, Cerrar);
// columna de canciones (fuentes con su estado); vista de la fuente elegida (forma de onda con
// compases, selección ajustada a compases o tiempos y a la transiente, escuchar, agregar al mix);
// línea del mix (tramos en orden con su forma de onda, uniones, cabezal) y panel del tramo
// elegido (mover, quitar, inicio y fin por compases, tempo, tono, ganancia, fundido, nombre).
class MixEditor : public juce::Component
{
public:
    MixEditor();
    ~MixEditor() override;

    // Proyecto a mostrar (no es dueño; nullptr = nada). Reinicia selecciones y zoom.
    void setProject (MixProject*);
    // El proyecto cambió desde fuera (fuentes agregadas o quitadas, análisis terminado): redibujar y
    // acotar las selecciones
    void refresh();

    // Forma de onda de una fuente (pares mín/máx, `binsPerSecond` por segundo), cuando está lista
    void setSourcePeaks (int source, std::vector<float> minMaxPairs, int binsPerSecond);
    // Estado de una fuente para la lista ("Analizando... 40 %", "Sin analizar", ""): vacío = mostrar
    // tempo, compás y tonalidad del análisis
    void setSourceStatus (int source, const juce::String& status);
    // Cabezal: qué suena (0 nada, 1 la fuente `source`, 2 el mix), en segundos de eso, y si suena
    void setPlayback (int kind, int source, double seconds, bool playing);
    void setStatus (const juce::String&);   // línea de estado al pie
    void setBusy (bool rendering);          // mientras se renderiza: deshabilita Crear canción

    int getSelectedSource() const  { return selectedSource; }
    int getSelectedSegment() const { return selectedSegment; }
    void selectSource (int);
    void selectSegment (int);

    // --- Avisos al dueño ---
    std::function<void()> onChanged;                              // tramos, tempo o análisis editados: guardar, render viejo
    std::function<void()> onAddSources;                           // "+ Canción": elegir archivos
    std::function<void (int source)> onRemoveSource;              // (el dueño confirma y quita)
    std::function<void (int source)> onAnalyzeSource;             // analizar de nuevo
    std::function<void (int source, double from)> onPlaySource;   // escuchar la fuente desde ese instante
    std::function<void (double mixSeconds)> onPlayMix;            // renderizar si hace falta y escuchar desde ahí
    std::function<void()> onStop;
    std::function<void (bool on)> onClickToggled;                 // click al escuchar (para comprobar los tiempos)
    std::function<void()> onCreateSong;
    std::function<void()> onClose;
    std::function<void (const juce::String& newName)> onRename;   // el dueño renombra la carpeta si puede
    // Ajusta un corte a la transiente (lo hace el dueño con mix::refineToOnset); si no está, no se ajusta
    std::function<double (int source, double seconds)> refineCut;

    // Atajos que el dueño reenvía (Espacio, Esc...); true si se usó
    bool handleKey (const juce::KeyPress&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    MixProject* project = nullptr;
    int selectedSource = -1, selectedSegment = -1;
    friend struct Impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixEditor)
};
