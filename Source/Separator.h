#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Library.h"
#include <atomic>
#include <vector>

// Qué se le pide a la separación: cuántas pistas y con cuánta calidad (tiempo).
struct SeparationOptions
{
    int stems = 4;               // 4 (voces, batería, bajo, otros) o 6 (+ guitarra, piano)
    int quality = 0;             // 0 normal, 1 alta (modelos afinados), 2 máxima (además promedia varias pasadas)
    bool roformerVocals = false; // voces con BS-Roformer (audio-separator) antes de Demucs, si está instalado
};

// Una ejecución de una herramienta externa dentro del plan.
struct SeparationStage
{
    juce::String tool;           // "demucs" o "roformer"
    juce::String model;          // nombre del modelo
    int shifts = 1;              // pasadas promediadas (Demucs --shifts)
    double overlap = 0.25;       // solapamiento entre trozos (Demucs --overlap)
    int passes = 1;              // barras de progreso que imprime (modelos de la bolsa x shifts)
    double weight = 1.0;         // costo relativo, para la barra de progreso global
};

// Separa una canción completa en stems ejecutando Demucs (IA) en segundo plano.
// Pipeline: convertir a WAV 44,1 kHz -> [Roformer: voces] -> Demucs (1 o 2 modelos) -> armar carpeta
// final donde "otros" es el residuo exacto (mezcla menos las demás pistas), así la suma de los stems
// reproduce la mezcla original sin pérdidas.
class Separator : private juce::Thread
{
public:
    enum class State { idle, running, done, failed, cancelled };

    Separator();
    ~Separator() override;

    bool start (const juce::File& input, const juce::String& pythonPath,
                const SeparationOptions& options, const juce::String& songName);
    void cancel();
    void reset();      // vuelve a idle y borra temporales (espera a que el hilo termine si hace falta)

    State getState() const        { return (State) state.load(); }
    float getProgress() const     { return progress.load(); }   // -1 = indeterminado
    juce::String getMessage() const;
    juce::File getResultFolder() const { return resultFolder; }
    juce::String getSongName() const   { return songName; }
    juce::String getPlanDescription() const { return describe (options); }   // válido tras start()
    juce::File getWorkFolder() const   { return workDir; }                   // temporales (diagnóstico)

    // --- Utilidades sin estado (con tests) ---
    // Etapas que se ejecutan para unas opciones dadas.
    static std::vector<SeparationStage> planFor (const SeparationOptions&);
    // Texto corto para la UI: "6 pistas, calidad alta: BS-Roformer + htdemucs_ft + htdemucs_6s".
    static juce::String describe (const SeparationOptions&);
    // Costo relativo total del plan (1 = una pasada de htdemucs).
    static double relativeCost (const SeparationOptions&);
    // true si en el mismo venv que "python" está instalado audio-separator (BS-Roformer).
    static bool isRoformerAvailable (const juce::String& pythonPath);
    // Porcentaje de una línea de tqdm (" 43%|####| ...") o -1 si la línea no trae progreso.
    static int parsePercent (const juce::String& line);
    // Cantidad de modelos en la bolsa de un modelo de Demucs (htdemucs_ft trae 4).
    static int modelsInBag (const juce::String& model);
    // Convierte cualquier archivo de audio a WAV de 24 bits (máximo 2 canales), remuestreando
    // con sinc enventanado si la frecuencia no coincide.
    static bool convertToWav (const juce::File& in, const juce::File& out, double targetRate, juce::String& error);
    // Escribe out = mix - suma (stems). Todos WAV con la misma frecuencia; el largo es el de mix.
    static bool writeResidual (const juce::File& mix, const juce::Array<juce::File>& stems,
                               const juce::File& out, juce::String& error);

    static constexpr double demucsSampleRate = 44100.0;   // frecuencia de trabajo de los modelos

private:
    void run() override;
    bool runStage (const SeparationStage&, const juce::File& input, const juce::File& outDir, juce::File& resultDir);
    bool runProcess (const juce::StringArray& args, int passes);
    void setStatus (State, const juce::String&);
    void handleOutputLine (const std::string& line);

    juce::File inputFile, workDir, resultFolder;
    juce::String python, songName;
    SeparationOptions options;

    std::atomic<int> state { (int) State::idle };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection messageLock, processLock;
    juce::String message;
    juce::ChildProcess* process = nullptr;

    // Solo hilo del separador
    juce::StringArray log;
    double totalWeight = 1.0, stageOffset = 0.0, stageWeight = 1.0;
    int passes = 1, pass = 0, lastPercent = -1;
};
