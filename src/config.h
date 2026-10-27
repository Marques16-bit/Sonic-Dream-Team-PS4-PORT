#pragma once
// ---------------------------------------------------------------------------
// Decisões fixas do port para PS4. Mexa aqui, não espalhe pelo código.
// ---------------------------------------------------------------------------

namespace cfg {

// Resolução interna de render. Saída HDMI também em 1080p (PS4 base/Pro).
constexpr int kRenderWidth  = 1920;
constexpr int kRenderHeight = 1080;

// Mesmo valor do render scale do jogo: 1.0 = nativo, sem upscaling dinâmico.
constexpr float kRenderScale = 1.0f;

// Qualidade gráfica travada no máximo (ignora o que o jogo salvou).
enum class Quality { Low, Medium, High, Max };
constexpr Quality kQuality = Quality::Max;

// Só DualShock 4: glifos de botão sempre PlayStation, sem detecção de outro controle.
constexpr bool kForceDualShockGlyphs = true;

// Esconde/ignora a opção de vídeo (resolução, fps, qualidade) no menu do jogo.
constexpr bool kHideVideoOptions = true;

// Alvo de frame. A física do jogo roda a 60 Hz fixos.
constexpr int kTargetFps = 60;

} // namespace cfg
