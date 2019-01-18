#include "Core.hpp"
#include "midi.hpp"
#include "event.hpp"


struct MIDITriggerToCVInterface : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(TRIG_OUTPUT, 16),
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	midi::InputQueue midiInput;

	bool gates[16];
	float gateTimes[16];
	uint8_t velocities[16];
	int learningId = -1;
	uint8_t learnedNotes[16] = {};
	bool velocity = false;

	MIDITriggerToCVInterface() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		onReset();
	}

	void onReset() override {
		for (int i = 0; i < 16; i++) {
			gates[i] = false;
			gateTimes[i] = 0.f;
			learnedNotes[i] = i + 36;
		}
		learningId = -1;
	}

	void pressNote(uint8_t note, uint8_t vel) {
		// Learn
		if (learningId >= 0) {
			learnedNotes[learningId] = note;
			learningId = -1;
		}
		// Find id
		for (int i = 0; i < 16; i++) {
			if (learnedNotes[i] == note) {
				gates[i] = true;
				gateTimes[i] = 1e-3f;
				velocities[i] = vel;
			}
		}
	}

	void releaseNote(uint8_t note) {
		// Find id
		for (int i = 0; i < 16; i++) {
			if (learnedNotes[i] == note) {
				gates[i] = false;
			}
		}
	}

	void step() override {
		midi::Message msg;
		while (midiInput.shift(&msg)) {
			processMessage(msg);
		}
		float deltaTime = app()->engine->getSampleTime();

		for (int i = 0; i < 16; i++) {
			if (gateTimes[i] > 0.f) {
				outputs[TRIG_OUTPUT + i].setVoltage(velocity ? rescale(velocities[i], 0, 127, 0.f, 10.f) : 10.f);
				// If the gate is off, wait 1 ms before turning the pulse off.
				// This avoids drum controllers sending a pulse with 0 ms duration.
				if (!gates[i]) {
					gateTimes[i] -= deltaTime;
				}
			}
			else {
				outputs[TRIG_OUTPUT + i].setVoltage(0.f);
			}
		}
	}

	void processMessage(midi::Message msg) {
		switch (msg.getStatus()) {
			// note off
			case 0x8: {
				releaseNote(msg.getNote());
			} break;
			// note on
			case 0x9: {
				if (msg.getValue() > 0) {
					pressNote(msg.getNote(), msg.getValue());
				}
				else {
					// Many stupid keyboards send a "note on" command with 0 velocity to mean "note release"
					releaseNote(msg.getNote());
				}
			} break;
			default: break;
		}
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_t *notesJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_t *noteJ = json_integer(learnedNotes[i]);
			json_array_append_new(notesJ, noteJ);
		}
		json_object_set_new(rootJ, "notes", notesJ);

		json_object_set_new(rootJ, "midi", midiInput.toJson());
		json_object_set_new(rootJ, "velocity", json_boolean(velocity));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *notesJ = json_object_get(rootJ, "notes");
		if (notesJ) {
			for (int i = 0; i < 16; i++) {
				json_t *noteJ = json_array_get(notesJ, i);
				if (noteJ)
					learnedNotes[i] = json_integer_value(noteJ);
			}
		}

		json_t *midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);

		json_t *velocityJ = json_object_get(rootJ, "velocity");
		if (velocityJ)
			velocity = json_boolean_value(velocityJ);
	}
};


struct MidiTrigChoice : GridChoice {
	MIDITriggerToCVInterface *module;
	int id;

	MidiTrigChoice() {
		box.size.y = mm2px(6.666);
		textOffset.y -= 4;
		textOffset.x -= 4;
	}

	void setId(int id) override {
		this->id = id;
	}

	void step() override {
		if (!module)
			return;
		if (module->learningId == id) {
			text = "LRN";
			color.a = 0.5;
		}
		else {
			uint8_t note = module->learnedNotes[id];
			static const char *noteNames[] = {
				"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
			};
			int oct = note / 12 - 1;
			int semi = note % 12;
			text = string::f("%s%d", noteNames[semi], oct);
			color.a = 1.0;

			if (app()->event->selectedWidget == this)
				app()->event->selectedWidget = NULL;
		}
	}

	void onSelect(const event::Select &e) override {
		e.consume(this);
		if (!module)
			return;
		module->learningId = id;
	}

	void onDeselect(const event::Deselect &e) override {
		if (!module)
			return;
		module->learningId = -1;
	}
};


struct MidiTrigWidget : Grid16MidiWidget {
	MIDITriggerToCVInterface *module;
	GridChoice *createGridChoice() override {
		MidiTrigChoice *gridChoice = new MidiTrigChoice;
		gridChoice->module = module;
		return gridChoice;
	}
};


struct MIDITriggerToCVInterfaceWidget : ModuleWidget {
	MIDITriggerToCVInterfaceWidget(MIDITriggerToCVInterface *module) : ModuleWidget(module) {
		setPanel(SVG::load(asset::system("res/Core/MIDITriggerToCVInterface.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(createOutput<PJ301MPort>(mm2px(Vec(3.894335, 73.344704)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 0));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(15.494659, 73.344704)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 1));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(27.094982, 73.344704)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 2));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(38.693932, 73.344704)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 3));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(3.8943355, 84.945023)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 4));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(15.49466, 84.945023)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 5));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(27.094982, 84.945023)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 6));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(38.693932, 84.945023)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 7));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(3.8943343, 96.543976)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 8));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(15.494659, 96.543976)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 9));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(27.09498, 96.543976)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 10));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(38.693932, 96.543976)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 11));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(3.894335, 108.14429)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 12));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(15.49466, 108.14429)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 13));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(27.09498, 108.14429)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 14));
		addOutput(createOutput<PJ301MPort>(mm2px(Vec(38.693932, 108.14429)), module, MIDITriggerToCVInterface::TRIG_OUTPUT + 15));

		MidiTrigWidget *midiWidget = createWidget<MidiTrigWidget>(mm2px(Vec(3.399621, 14.837339)));
		midiWidget->module = module;
		midiWidget->box.size = mm2px(Vec(44, 54.667));
		if (module)
			midiWidget->midiIO = &module->midiInput;
		midiWidget->createGridChoices();
		addChild(midiWidget);
	}

	void appendContextMenu(Menu *menu) override {
		MIDITriggerToCVInterface *module = dynamic_cast<MIDITriggerToCVInterface*>(this->module);

		struct VelocityItem : MenuItem {
			MIDITriggerToCVInterface *module;
			void onAction(const event::Action &e) override {
				module->velocity ^= true;
			}
		};

		menu->addChild(new MenuEntry);
		VelocityItem *velocityItem = createMenuItem<VelocityItem>("Velocity", CHECKMARK(module->velocity));
		velocityItem->module = module;
		menu->addChild(velocityItem);
	}
};


Model *modelMIDITriggerToCVInterface = createModel<MIDITriggerToCVInterface, MIDITriggerToCVInterfaceWidget>("MIDITriggerToCVInterface");
