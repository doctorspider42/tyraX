// props_gallery.cpp - Script cookbook, row F: EVERY kind of public field the
// editor can fill in ([[tyra::prop]]), each one used for something you can
// see, plus a logger that prints every lifecycle callback as it happens.
#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// F1. PropsGallery - select it and look at Properties > Scripts: one widget
// per field kind. Change any of them, Build & Run, and press USE on it -
// the log prints what the game received.
// ---------------------------------------------------------------------------
class PropsGallery : public ObjectScript {
 public:
  // --- numbers
  [[tyra::prop(header = "Numbers", min = 0, max = 10, tooltip = "float with min+max = a slider")]]
  float sliderFloat = 2.5F;
  [[tyra::prop(tooltip = "float without limits = a drag field")]]
  float dragFloat = -1.0F;
  [[tyra::prop(min = 1, max = 8, tooltip = "int with min+max = an integer slider")]]
  int spinCount = 3;
  [[tyra::prop(tooltip = "bool = a checkbox")]]
  bool spinning = true;
  [[tyra::prop(options = "Still|Bob|Spin|Both", tooltip = "int + options = a drop-down")]]
  int mode = 3;

  // --- vectors and colours
  [[tyra::prop(header = "Vectors", tooltip = "tyrax::Vec3 = three drags")]]
  tyrax::Vec3 offset{0.0F, 0.5F, 0.0F};
  [[tyra::prop(color, tooltip = "tyrax::Vec3 + color = a colour picker")]]
  tyrax::Vec3 tint{0.95F, 0.45F, 0.85F};

  // --- text
  [[tyra::prop(header = "Text", tooltip = "const char* = a text field")]]
  const char* label = "Hello from the gallery";

  // --- references into the project
  [[tyra::prop(header = "References", tooltip = "tyrax::ObjectRef = an object of this scene")]]
  tyrax::ObjectRef buddy;
  [[tyra::prop(tooltip = "tyrax::SoundId = a project sound")]]
  tyrax::SoundId sound = {-1};
  [[tyra::prop(tooltip = "tyrax::EventId = a graph or script event")]]
  tyrax::EventId event = {-1};
  [[tyra::prop(tooltip = "tyrax::SceneId = a scene")]]
  tyrax::SceneId scene = {-1};
  [[tyra::prop(tooltip = "tyrax::SaveValueId = a save value")]]
  tyrax::SaveValueId saveValue = {-1};
  [[tyra::prop(tooltip = "tyrax::ActionId = an Input Map action")]]
  tyrax::ActionId action = {-1};
  [[tyra::prop(tooltip = "tyrax::HudTextId = a HUD text")]]
  tyrax::HudTextId hudText = {-1};
  [[tyra::prop(tooltip = "tyrax::HudBarId = a HUD bar")]]
  tyrax::HudBarId hudBar = {-1};
  [[tyra::prop(tooltip = "tyrax::MenuId = a game menu (opened while you hold the action)")]]
  tyrax::MenuId menu = {-1};

  void onStart(ScriptContext&) override {
    base = object.position();
    object.setColor(tint);
    if (spinning) object.spin(tyrax::Vec3(0.0F, 30.0F * (float)spinCount, 0.0F));
    printFields();
  }

  void onUpdate(ScriptContext& ctx) override {
    // mode: 0 still, 1 bob, 2 spin, 3 both - an enum field driving behaviour.
    const bool bob = mode == 1 || mode == 3;
    object.setPosition(base + offset +
                       tyrax::Vec3(0.0F, bob ? 0.3F * sinf(tyrax::sceneTime() * sliderFloat) : 0.0F,
                                   0.0F));
    if (buddy) buddy.lookAt(object.position());  // the ObjectRef field in use
    if (tyrax::actionPressed(ctx, action) &&
        tyrax::distanceToPlayer(ctx, object.position()) < 4.0F)
      tyrax::openMenu(ctx, menu);
  }

  void onUsed(ScriptContext& ctx) override {
    tyrax::playSound(ctx, sound);
    tyrax::sendEvent(event, sliderFloat);
    tyrax::addSaveValue(ctx, saveValue, 1.0F);
    tyrax::setHudBar(ctx, hudBar, tyrax::saveValue(ctx, saveValue));
    tyrax::showText(ctx, hudText, 1.5F);
    printFields();
  }

 private:
  void printFields() const {
    tyrax::debugLog("COOKBOOK gallery slider=", sliderFloat, " drag=", dragFloat,
                    " count=", spinCount, " spinning=", (int)spinning, " mode=", mode,
                    " offset=", offset.x, ",", offset.y, ",", offset.z, " tint=", tint.x, ",",
                    tint.y, ",", tint.z, " label=", label, " buddy=", buddy.index(),
                    " sound=", sound.index, " event=", event.index, " scene=", scene.index,
                    " save=", saveValue.index, " action=", action.index, " text=", hudText.index,
                    " bar=", hudBar.index, " menu=", menu.index);
  }
  tyrax::Vec3 base;
};
TYRA_OBJECT_SCRIPT(PropsGallery);

// ---------------------------------------------------------------------------
// F2. LifecycleLogger - prints every ObjectScript callback the moment it is
// called. Put it on an Area to see onPlayerEnter / onPlayerExit too.
// ---------------------------------------------------------------------------
class LifecycleLogger : public ObjectScript {
 public:
  [[tyra::prop(tooltip = "Also log onLateUpdate once a second")]]
  bool logLateUpdate = false;

  void onStart(ScriptContext&) override { log("onStart"); }
  void onUpdate(ScriptContext&) override { ++frames; }
  void onLateUpdate(ScriptContext&) override {
    if (logLateUpdate && frames % 50 == 0) log("onLateUpdate");
  }
  void onUsed(ScriptContext&) override { log("onUsed"); }
  void onEvent(ScriptContext&, tyrax::EventId e, float v) override {
    tyrax::debugLog("COOKBOOK life onEvent index=", e.index, " value=", v);
  }
  void onPlayerEnter(ScriptContext&) override { log("onPlayerEnter"); }
  void onPlayerExit(ScriptContext&) override { log("onPlayerExit"); }
  void onAnimationFinished(ScriptContext&) override { log("onAnimationFinished"); }
  void onActivated(ScriptContext&) override { log("onActivated"); }
  void onDeactivated(ScriptContext&) override { log("onDeactivated"); }
  void onSceneEnd(ScriptContext&) override { log("onSceneEnd"); }

 private:
  void log(const char* what) const {
    tyrax::debugLog("COOKBOOK life ", what, " frame=", frames, " t=", tyrax::sceneTime());
  }
  unsigned int frames = 0;
};
TYRA_OBJECT_SCRIPT(LifecycleLogger);

TYRAX_NAMESPACE_END
