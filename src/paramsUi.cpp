/*
 * OSARA: Open Source Accessibility for the REAPER Application
 * Parameters UI code
 * Author: James Teh <jamie@jantrid.net>
 * Copyright 2014-2023 NV Access Limited, James Teh
 * License: GNU General Public License version 2.0
 */

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <regex>
// osara.h includes windows.h, which must be included before other Windows
// headers.
#include "osara.h"
#ifdef _WIN32
#include <initguid.h>
#include <Windowsx.h>
#include <Commctrl.h>
#endif
#include <WDL/win32_utf8.h>
#include <WDL/db2val.h>
#include <WDL/wdltypes.h>
#include <reaper/reaper_plugin.h>
#include "config.h"
#include "fxChain.h"
#include "resource.h"
#include "translation.h"

using namespace std;

class Param {
	public:
	double min;
	double max;
	double step;
	double largeStep;
	bool isEditable;

	Param(): isEditable(false) {
	}

	virtual ~Param() = default;

	virtual double getValue() = 0;
	virtual string getValueText(double value) = 0;
	virtual string getValueForEditing() {
		return "";
	}
	virtual void setValue(double value) = 0;
	virtual void setValueFromEdited(const string& text) {
	}
};

class ParamSource {
	public:
	virtual ~ParamSource() = default;
	virtual string getTitle() = 0;
	virtual int getParamCount() = 0;
	virtual string getParamName(int param) = 0;
	virtual unique_ptr<Param> getParam(int param) = 0;
};

// Provides data for a parameter and allows you to create a Param instance for
// it. Used where the parameters are predefined; e.g. for tracks and items.
class ParamProvider {
	public:
	ParamProvider(const string displayName): displayName(displayName) {
	}

	virtual ~ParamProvider() = default;

	virtual unique_ptr<Param> makeParam() = 0;

	const string displayName;
};

class ReaperObjParamProvider;
typedef unique_ptr<Param>(*MakeParamFromProviderFunc)(ReaperObjParamProvider&);
class ReaperObjParamProvider: public ParamProvider {
	public:
	ReaperObjParamProvider(const string displayName, const string name,
		MakeParamFromProviderFunc makeParamFromProvider):
		ParamProvider(displayName), name(name),
		makeParamFromProvider(makeParamFromProvider) {}

	virtual unique_ptr<Param> makeParam() override {
		return makeParamFromProvider(*this);
	}

	virtual void* getSetValue(void* newValue) = 0;

	protected:
	const string name;

	private:
	MakeParamFromProviderFunc makeParamFromProvider;
};

class ReaperObjParam: public Param {
	protected:
	ReaperObjParamProvider& provider;

	ReaperObjParam(ReaperObjParamProvider& provider):
		Param(), provider(provider) {}
};

class ReaperObjParamSource: public ParamSource {
	protected:
	vector<unique_ptr<ParamProvider>> params;

	public:

	int getParamCount() {
		return (int)this->params.size();
	}

	string getParamName(int param) {
		return this->params[param]->displayName;
	}

	unique_ptr<Param> getParam(int param) {
		return this->params[param]->makeParam();
	}

};

class ReaperObjToggleParam: public ReaperObjParam {
	public:
	ReaperObjToggleParam(ReaperObjParamProvider& provider ):
			ReaperObjParam(provider) {
		this->min = 0;
		this->max = 1;
		this->step = 1;
		this->largeStep = 1;
	}

	double getValue() {
		return (double)*(bool*)this->provider.getSetValue(nullptr);
	}

	string getValueText(double value) {
		return value ?
			// Translators: Reported in Parameters dialogs for a toggle (such as mute)
			// which is on.
			translate("on") :
			// Translators: Reported in Parameters dialogs for a toggle (such as mute)
			// which is off.
			translate("off");
	}

	void setValue(double value) {
		bool val = (bool)value;
		this->provider.getSetValue((void*)&val);
	}

	static unique_ptr<Param> make(ReaperObjParamProvider& provider) {
		return make_unique<ReaperObjToggleParam>(provider);
	}
};

class ReaperObjVolParam: public ReaperObjParam {
	public:
	ReaperObjVolParam(ReaperObjParamProvider& provider ):
			ReaperObjParam(provider) {
		this->min = 0;
		this->max = 4;
		this->step = 0.002;
		this->largeStep = 0.1;
		this->isEditable = true;
		if (this->getValue() < 0) {
			// Take volume raw values are negative when the polarity is flipped.
			this->flipSign = true;
		}
	}

	double getValue() {
		double result = *(double*)this->provider.getSetValue(nullptr);
		if (this->flipSign) {
			result = -result;
		}
		return result;
	}

	string getValueText(double value) {
		char out[64];
		mkvolstr(out, value);
		return out;
	}

	string getValueForEditing() {
		return this->getValueText(this->getValue());
	}

	void setValue(double value) {
		if (this->flipSign) {
			double flipped = -value;
			this->provider.getSetValue((void*)&flipped);
		} else {
			this->provider.getSetValue((void*)&value);
		}
	}

	void setValueFromEdited(const string& text) {
		if (text.compare(0, 4, "-inf") == 0) {
			this->setValue(0);
			return;
		}
		double db = atof(text.c_str());
		this->setValue(DB2VAL(db));
	}

	static unique_ptr<Param> make(ReaperObjParamProvider& provider) {
		return make_unique<ReaperObjVolParam>(provider);
	}

	private:
	bool flipSign = false;
};

class ReaperObjPanParam: public ReaperObjParam {
	public:
	ReaperObjPanParam(ReaperObjParamProvider& provider ):
			ReaperObjParam(provider) {
		this->min = -1;
		this->max = 1;
		this->step = 0.01;
		this->largeStep = 0.1;
		this->isEditable = true;
	}

	double getValue() {
		return *(double*)this->provider.getSetValue(nullptr);
	}

	string getValueText(double value) {
		char out[64];
		mkpanstr(out, value);
		return out;
	}

	string getValueForEditing() {
		return this->getValueText(this->getValue());
	}

	void setValue(double value) {
		this->provider.getSetValue((void*)&value);
	}

	void setValueFromEdited(const string& text) {
		this->setValue(parsepanstr(text.c_str()));
	}

	static unique_ptr<Param> make(ReaperObjParamProvider& provider) {
		return make_unique<ReaperObjPanParam>(provider);
	}
};

class ReaperObjLenParam: public ReaperObjParam {
	public:
	ReaperObjLenParam(ReaperObjParamProvider& provider ):
			ReaperObjParam(provider) {
		this->min = 0;
		this->max = 500;
		this->step = 0.02;
		this->largeStep = 10;
		this->isEditable = true;
		resetTimeCache();
	}

	double getValue() {
		return *(double*)this->provider.getSetValue(nullptr);
	}

	string getValueText(double value) {
		static string lastText;
		string text = formatTime(value, TF_RULER, true);
		if (text.empty()) {
			// formatTime returned nothing because value produced the same value text as the last call.
			// Therefore, we cache the text and return it here.
			return lastText;
		}
		lastText = text;
		return text;
	}

	string getValueForEditing() {
		char out[64];
		format_timestr_pos(this->getValue(), out, sizeof(out), -1);
		return out;
	}

	void setValue(double value) {
		this->provider.getSetValue((void*)&value);
	}

	void setValueFromEdited(const string& text) {
		this->setValue(parse_timestr_pos(text.c_str(), -1));
	}

	static unique_ptr<Param> make(ReaperObjParamProvider& provider) {
		return make_unique<ReaperObjLenParam>(provider);
	}
};

const char CFGKEY_DIALOG_POS[] = "paramsDialogPos";

bool isParamsDialogOpen = false;

class ParamsDialog {
	private:
	unique_ptr<ParamSource> source;
	HWND dialog;
	HWND paramCombo;
	HWND slider;
	HWND valueEdit;
	HWND valueLabel;
	int paramCount;
	string filter;
	vector<int> visibleParams;
	int paramNum;
	unique_ptr<Param> param;
	double val;
	string valText;
	HWND prevFocus;
	bool isDestroying = false;
	bool suppressValueChangeReport = false;

	void updateValueText() {
		if (this->valText.empty()) {
			// Fall back to a percentage.
			double percent = (this->val - this->param->min)
				/ (this->param->max - this->param->min) * 100;
			ostringstream s;
			s << fixed << setprecision(1) << percent << "%";
			this->valText = s.str();
		}
#ifdef _WIN32
		// Set the slider's accessible value to this text.
		accPropServices->SetHwndPropStr(this->slider, OBJID_CLIENT, CHILDID_SELF,
			PROPID_ACC_VALUE, widen(this->valText).c_str());
		if (!this->suppressValueChangeReport) {
			NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, this->slider,
				OBJID_CLIENT, CHILDID_SELF);
		}
#else // _WIN32
		// We can't set the slider's accessible value on Mac.
		if (!this->suppressValueChangeReport) {
			outputMessage(this->valText);
		}
#endif // _WIN32
		SetWindowText(this->valueLabel, this->valText.c_str());
	}

	void updateValue() {
		this->valText = this->param->getValueText(this->val);
		this->updateValueText();
		if (this->param->isEditable) {
			SetWindowText(this->valueEdit, this->param->getValueForEditing().c_str());
		}
	}

	void onParamChange() {
		this->paramNum = this->visibleParams[ComboBox_GetCurSel(this->paramCombo)];
		this->param = this->source->getParam(this->paramNum);
		this->val = this->param->getValue();
		EnableWindow(this->valueEdit, this->param->isEditable);
		this->updateValue();
	}

	void onSliderChange(double newVal) {
		if (newVal == this->val
				|| newVal < this->param->min || newVal > this->param->max) {
			return;
		}
		double step = this->param->step;
		if (newVal < val) {
			step = -step;
		}
		this->val = newVal;

		// If the value text (if any) doesn't change, the value change is insignificant.
		// Snap to the next change in value text.
		// Continually adding to a float accumulates inaccuracy, so multiply by the
		// number of steps each iteration instead.
		for (unsigned int steps = 1;
			this->param->min <= newVal && newVal <= this->param->max;
			newVal = this->val + (step * steps++)
		) {
			const string testText = this->param->getValueText(newVal);
			if (testText.empty())
				break; // Formatted values not supported.
			if (testText.compare(this->valText) != 0) {
				// The value text is different, so this change is significant.
				// Snap to this value.
				this->val = newVal;
				break;
			}
		}
		this->param->setValue(this->val);
		this->updateValue();
	}

	void onValueEdited() {
		char rawText[30];
		if (GetDlgItemText(dialog, ID_PARAM_VAL_EDIT, rawText, sizeof(rawText)) == 0)
			return;
		if (this->param->getValueForEditing().compare(rawText) == 0)
			return;
		this->param->setValueFromEdited(rawText);
		this->val = this->param->getValue();
		this->updateValue();
	}

	void saveWindowPos() {
		RECT rect;
		GetWindowRect(this->dialog, &rect);
		ostringstream s;
		s << rect.left << " " << rect.top << " " <<
			(rect.right - rect.left) << " " << (rect.bottom - rect.top);
		SetExtState(CONFIG_SECTION, CFGKEY_DIALOG_POS, s.str().c_str(), true);
	}

	void restoreWindowPos() {
		const char* config = GetExtState(CONFIG_SECTION, CFGKEY_DIALOG_POS);
		if (!config[0]) {
			return;
		}
		istringstream s(config);
		int x, y, w, h;
		s >> x >> y >> w >> h;
		SetWindowPos(this->dialog, nullptr, x, y, w, h,
			SWP_NOACTIVATE | SWP_NOZORDER);
	}

	static INT_PTR CALLBACK dialogProc(HWND dialogHwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		ParamsDialog* dialog = (ParamsDialog*)GetWindowLongPtr(dialogHwnd, GWLP_USERDATA);
		switch (msg) {
			case WM_COMMAND:
				if (LOWORD(wParam) == ID_PARAM && HIWORD(wParam) == CBN_SELCHANGE) {
					dialog->onParamChange();
					return TRUE;
				} else if (LOWORD(wParam) == ID_PARAM_FILTER && HIWORD(wParam) == EN_KILLFOCUS) {
					dialog->onFilterChange();
					return TRUE;
				} else if (LOWORD(wParam) == ID_PARAM_VAL_EDIT && HIWORD(wParam) ==EN_KILLFOCUS) {
					dialog->onValueEdited();
					return TRUE;
				} else if (LOWORD(wParam) == ID_PARAM_UNNAMED) {
					dialog->updateParamList();
					return TRUE;
				} else if (LOWORD(wParam) == IDCANCEL) {
					dialog->saveWindowPos();
					dialog->isDestroying = true;
					DestroyWindow(dialogHwnd);
					delete dialog;
					return TRUE;
				}
				break;
			case WM_CLOSE:
				dialog->saveWindowPos();
				dialog->isDestroying = true;
				DestroyWindow(dialogHwnd);
				delete dialog;
				return TRUE;
			case WM_ACTIVATE:
				if (!dialog->isDestroying && LOWORD(wParam) == WA_INACTIVE) {
					// If something steals focus, close the dialog. Otherwise, we won't
					// unregister the key hook,  surface feedback won't report FX parameter
					// changes and there will be a dialog left open the user can't get to
					// easily.
					// Do not try to restore focus as we close.
					dialog->prevFocus = nullptr;
					PostMessage(dialogHwnd, WM_CLOSE, 0, 0);
					return TRUE;
				}
		}
		return FALSE;
	}

	accelerator_register_t accelReg;
	static int translateAccel(MSG* msg, accelerator_register_t* accelReg) {
		ParamsDialog* dialog = (ParamsDialog*)accelReg->user;
		if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) {
			return 0; // Not interested.
		}
		if (msg->hwnd == dialog->slider) {
			// We handle key presses for the slider ourselves.
			double newVal = dialog->val;
			bool consumed = true;
			switch (msg->wParam) {
				case VK_UP:
				case VK_RIGHT:
					newVal += dialog->param->step;
					break;
				case VK_DOWN:
				case VK_LEFT:
					newVal -= dialog->param->step;
					break;
				case VK_PRIOR:
					newVal += dialog->param->largeStep;
					break;
				case VK_NEXT:
					newVal -= dialog->param->largeStep;
					break;
				case VK_HOME:
					newVal = dialog->param->max;
					break;
				case VK_END:
					newVal = dialog->param->min;
					break;
				default:
					consumed = false;
			}
			if (consumed) {
				dialog->onSliderChange(newVal);
				return 1; // Eat the keystroke.
			}
		}
#ifdef _WIN32
		const bool control = GetAsyncKeyState(VK_CONTROL) & 0x8000;
#else
		// On Mac, SWELL maps the control key to VK_LWIN.
		const bool control = GetAsyncKeyState(VK_LWIN) & 0x8000;
#endif
		const bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
		if (msg->wParam == VK_TAB && control) {
			// Control+tab switches to the next parameter, control+shift+tab to the
			// previous.
			int newParam = ComboBox_GetCurSel(dialog->paramCombo) +
				(shift ? -1 : 1);
			if (newParam < 0) {
				newParam = dialog->visibleParams.size() - 1;
			} else if (newParam == dialog->visibleParams.size()) {
				newParam = 0;
			}
			// newParam could be -1 if there are no visible parameters.
			if (newParam >= 0) {
				ComboBox_SetCurSel(dialog->paramCombo, newParam);
				dialog->suppressValueChangeReport = true;
				dialog->onParamChange();
				dialog->suppressValueChangeReport = false;
				ostringstream s;
				s << dialog->source->getParamName(dialog->paramNum) << ", " <<
					dialog->valText;
				outputMessage(s);
			}
			return 1; // Eat the keystroke.
		}
		if (msg->wParam == VK_SPACE) {
			// Let REAPER handle the space key so control+space works.
			return 0; // Not interested.
		}
		const bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
		if (msg->hwnd == dialog->paramCombo ||
				isClassName(GetFocus(), "Edit")) {
			// In text boxes and combo boxes, we only allow specific keys through to
			// the main section.
			if (
				// A function key.
				(VK_F1 <= msg->wParam && msg->wParam <= VK_F12) ||
				// Anything with both alt and shift.
				(alt && shift)
			) {
				return -666; // Force to main window.
			}
			// Anything else must go to our window so the user can interact with the
			// control.
			return -1; // Pass to our window.
		}
		if (alt && 'A' <= msg->wParam && msg->wParam <= 'Z') {
			// Alt+letter could be an access key in our dialog; e.g. alt+p to focus
			// the Parameter combo box.
			return -1; // Pass to our window.
		}
		switch (msg->wParam) {
			// These keys are required to interact with the dialog.
			case VK_TAB:
			case VK_RETURN:
			case VK_ESCAPE:
			// UP and down arrows switch tracks, which changes focus, which dismisses
			// the dialog. This is an easy mistake to make, so prevent it.
			case VK_UP:
			case VK_DOWN:
				return -1; // Pass to our window.
		}
		return -666; // Force to main window.
	}

	~ParamsDialog() {
		plugin_register("-accelerator", &this->accelReg);
		isParamsDialogOpen = false;
		// Try to restore focus back to where it was when the dialog was opened.
		// This is particularly useful in the FX chain dialog because this doesn't
		// regain focus by itself if something else (like us) steals the focus.
		if (this->prevFocus) {
			SetFocus(this->prevFocus);
		}
	}

	const regex RE_UNNAMED_PARAM{"(?:|-|[P#]\\d{3}) \\(\\d+\\)"};
	bool shouldIncludeParam(string name) {
		if (!IsDlgButtonChecked(this->dialog, ID_PARAM_UNNAMED)) {
			smatch m;
			regex_match(name, m, RE_UNNAMED_PARAM);
			if (!m.empty()) {
				return false;
			}
		}
		if (filter.empty())
			return true;
		// Convert param name to lower case for match.
		transform(name.begin(), name.end(), name.begin(), ::tolower);
		return name.find(filter) != string::npos;
	}

	void updateParamList() {
		int prevSelParam;
		if (this->visibleParams.empty())
			prevSelParam = -1;
		else
			prevSelParam = this->visibleParams[ComboBox_GetCurSel(this->paramCombo)];
		this->visibleParams.clear();
		// Use the first item if the previously selected param gets filtered out.
		int newComboSel = 0;
		ComboBox_ResetContent(this->paramCombo);
		for (int p = 0; p < this->paramCount; ++p) {
			const string name = source->getParamName(p);
			if (!this->shouldIncludeParam(name))
				continue;
			this->visibleParams.push_back(p);
			ComboBox_AddString(this->paramCombo, name.c_str());
			if (p == prevSelParam)
				newComboSel = (int)this->visibleParams.size() - 1;
		}
		ComboBox_SetCurSel(this->paramCombo, newComboSel);
		if (this->visibleParams.empty()) {
			EnableWindow(this->slider, FALSE);
			return;
		}
		EnableWindow(this->slider, TRUE);
		this->onParamChange();
	}

	void onFilterChange() {
		char rawText[100];
		GetDlgItemText(this->dialog, ID_PARAM_FILTER, rawText, sizeof(rawText));
		string text = rawText;
		// We want to match case insensitively, so convert to lower case.
		transform(text.begin(), text.end(), text.begin(), ::tolower);
		if (this->filter.compare(text) == 0)
			return; // No change.
		this->filter = text;
		this->updateParamList();
	}

	public:

	ParamsDialog(unique_ptr<ParamSource> source): source(std::move(source)) {
		this->paramCount = this->source->getParamCount();
		if (this->paramCount == 0) {
			delete this;
			return;
		}
		this->prevFocus = GetFocus();
		this->dialog = CreateDialog(pluginHInstance, MAKEINTRESOURCE(ID_PARAMS_DLG), mainHwnd, ParamsDialog::dialogProc);
		translateDialog(this->dialog);
		SetWindowLongPtr(this->dialog, GWLP_USERDATA, (LONG_PTR)this);
		SetWindowText(this->dialog, this->source->getTitle().c_str());
		this->paramCombo = GetDlgItem(this->dialog, ID_PARAM);
		WDL_UTF8_HookComboBox(this->paramCombo);
		this->slider = GetDlgItem(this->dialog, ID_PARAM_VAL_SLIDER);
		// We need to do exotic stuff with this slider that we can't support on Mac:
		// 1. Custom step values (TBM_SETLINESIZE, TBM_SETPAGESIZE).
		// 2. Down arrow moving left instead of right (TBS_DOWNISLEFT).
		// We also snap to changes in value text, which is even tricky on Windows.
		// Therefore, we just use the slider as a placeholder and handle key
		// presses ourselves.
		// We also use this key handler to pass some keys through to the main
		// window.
		this->accelReg.translateAccel = &this->translateAccel;
		this->accelReg.isLocal = true;
		this->accelReg.user = (void*)this;
		plugin_register("accelerator", &this->accelReg);
		this->valueEdit = GetDlgItem(this->dialog, ID_PARAM_VAL_EDIT);
		this->valueLabel = GetDlgItem(this->dialog, ID_PARAM_VAL_LABEL);
		CheckDlgButton(this->dialog, ID_PARAM_UNNAMED, BST_CHECKED);
		this->updateParamList();
		this->restoreWindowPos();
		ShowWindow(this->dialog, SW_SHOWNORMAL);
		isParamsDialogOpen = true;
	}

};

// The FX functions in the REAPER API are the same for tracks and takes
// except for the prefix (TrackFX_*/TakeFX_*)
// and the first argument type (MediaTrack*/MediaItem_Take*).
// Deal with the type using templates
// and with the prefix by passing it and fetching the functions dynamically.
template<typename ReaperObj>
class FxParam;
template<typename ReaperObj>
class FxNamedConfigParam;

template<typename ReaperObj>
class FxParams: public ParamSource {
	friend class FxParam<ReaperObj>;
	friend class FxNamedConfigParam<ReaperObj>;

	private:
	ReaperObj* obj;
	int fx;
	// Named config params can't be enumerated, so we have to build a list of
	// these based on the effect and the known named parameters it supports. See
	// initNamedConfigParams().
	vector<FxNamedConfigParam<ReaperObj>> namedConfigParams;
	int (*_GetNumParams)(ReaperObj*, int);
	bool (*_GetFXName)(ReaperObj*, int, char*, int);
	bool (*_GetParamName)(ReaperObj*, int, int, char*, int);
	double (*_GetParam)(ReaperObj*, int, int, double*, double*);
	bool (*_GetParameterStepSizes)(ReaperObj*, int, int, double*, double*,
		double*, bool*);
	bool (*_SetParam)(ReaperObj*, int, int, double);
	bool (*_FormatParamValue)(ReaperObj*, int, int, double, char*, int);
	bool (*_GetNamedConfigParm)(ReaperObj*, int, const char*, char*, int);
	bool (*_SetNamedConfigParm)(ReaperObj*, int, const char*, const char*);

	void initNamedConfigParams();

	public:

	FxParams(ReaperObj* obj, const string& apiPrefix, int fx=-1):
			obj(obj), fx(fx) {
		// Get functions.
		*(void**)&this->_GetNumParams = plugin_getapi((apiPrefix + "_GetNumParams").c_str());
		*(void**)&this->_GetFXName = plugin_getapi(
			(apiPrefix + "_GetFXName").c_str());
		*(void**)&this->_GetParamName = plugin_getapi((apiPrefix + "_GetParamName").c_str());
		*(void**)&this->_GetParam = plugin_getapi((apiPrefix + "_GetParam").c_str());
		*(void**)&this->_GetParameterStepSizes = plugin_getapi((apiPrefix +
			"_GetParameterStepSizes").c_str());
		*(void**)&this->_SetParam = plugin_getapi((apiPrefix + "_SetParam").c_str());
		*(void**)&this->_FormatParamValue = plugin_getapi((apiPrefix + "_FormatParamValue").c_str());
		*(void**)&this->_GetNamedConfigParm = plugin_getapi(
			(apiPrefix + "_GetNamedConfigParm").c_str());
		*(void**)&this->_SetNamedConfigParm = plugin_getapi(
			(apiPrefix + "_SetNamedConfigParm").c_str());
		if (fx >= 0) {
			this->initNamedConfigParams();
		}
	}

	string getTitle() {
		return translate("FX Parameters");
	}

	int getParamCount() {
		// Any named config params come first, followed by normal params.
		return (int)this->namedConfigParams.size() +
			this->_GetNumParams(this->obj, this->fx);
	}

	string getParamName(int param) {
		ostringstream ns;
		auto namedCount = (int)this->namedConfigParams.size();
		if (param < namedCount) {
			ns << this->namedConfigParams[param].getDisplayName();
		} else {
			char name[256];
			this->_GetParamName(this->obj, this->fx, param - namedCount, name,
				sizeof(name));
			ns << name;
		}
		// Append the parameter number to facilitate efficient navigation
		// and to ensure reporting where two consecutive parameters have the same name (#32).
		ns << " (" << param << ")";
		return ns.str();
	}

	unique_ptr<Param> getParam(int fx, int param);
	unique_ptr<Param> getParam(int param) {
		auto namedCount = (int)this->namedConfigParams.size();
		if (param < namedCount) {
			return make_unique<FxNamedConfigParam<ReaperObj>>(
				this->namedConfigParams[param]);
		}
		return this->getParam(this->fx, param - namedCount);
	}
};

template<typename ReaperObj>
class FxParam: public Param {
	private:
	FxParams<ReaperObj>& source;
	int fx;
	int param;

	public:

	FxParam(FxParams<ReaperObj>& source, int fx, int param):
			Param(), source(source), fx(fx), param(param) {
		source._GetParam(source.obj, fx, param, &this->min, &this->max);
		// *FX_GetParameterStepSizes doesn't set these to 0 if it can't fetch them,
		// even if it returns true.
		this->step = 0;
		this->largeStep = 0;
		source._GetParameterStepSizes(source.obj, fx, param, &this->step, nullptr,
			&this->largeStep, nullptr);
		if (this->step) {
			if (!this->largeStep) {
				this->largeStep = (this->max - this->min) / 50;
				// Ensure largeStep is a multiple of step.
				this->largeStep = this->step * int(this->largeStep / this->step);
				if (this->largeStep == 0) {
					this->largeStep = this->step;
				}
			}
		} else {
			this->step = (this->max - this->min) / 1000;
			this->largeStep = this->step * 20;
		}
		this->isEditable = true;
		// Set this as the last touched FX and FX parameter, as well as the last
		// focused FX.
		string paramStr = format("{}", param);
		source._SetNamedConfigParm(source.obj, fx, "last_touched", paramStr.c_str());
		source._SetNamedConfigParm(source.obj, fx, "focused", "1");
	}

	double getValue() {
		return this->source._GetParam(this->source.obj, this->fx, this->param,
			nullptr, nullptr);
	}

	string getValueText(double value) {
		char text[50];
		if (this->source._FormatParamValue(this->source.obj, this->fx, param, value,
				text, sizeof(text)))
			return text;
		return "";
	}

	string getValueForEditing() {
		ostringstream s;
		s << fixed << setprecision(4);
		s << this->source._GetParam(this->source.obj, this->fx, this->param,
			nullptr, nullptr);
		return s.str();
	}

	void setValue(double value) {
		this->source._SetParam(this->source.obj, this->fx, this->param, value);
	}

	void setValueFromEdited(const string& text) {
		this->setValue(atof(text.c_str()));
	}
};

// The possible values for an FX named config param. The first string is the
// display name. The second is the name to pass to the API.
using FxNamedConfigParamValues = vector<pair<const char*, const char*>>;

template<typename ReaperObj>
class FxNamedConfigParam: public Param {
	private:
	FxParams<ReaperObj>& source;
	int fx;
	const string displayName;
	const string name;
	const FxNamedConfigParamValues& values;

	public:

	FxNamedConfigParam(FxParams<ReaperObj>& source,
			const string displayName, const string name,
			const FxNamedConfigParamValues& values):
			Param(), source(source), displayName(displayName), name(name),
			values(values) {
		this->min = 0;
		this->max = (double)values.size() - 1;
		this->step = 1;
		this->largeStep = 1;
		this->isEditable = false;
		// Set this as the last touched and focused FX. We can't set named parameters
		// as the last touched parameter, so just use the first numbered parameter
		// (0).
		source._SetNamedConfigParm(source.obj, fx, "last_touched", "0");
		source._SetNamedConfigParm(source.obj, fx, "focused", "1");
	}

	double getValue() {
		char valueStr[50];
		valueStr[0] = '\0';
		this->source._GetNamedConfigParm(this->source.obj, this->source.fx,
			this->name.c_str(), valueStr, sizeof(valueStr));
		if (!valueStr[0]) {
			return 0.0;
		}
		for (int v = 0; v < (int)this->values.size(); ++v) {
			if (strcmp(valueStr, this->values[v].second) == 0) {
				return v;
			}
		}
		return 0.0;
	}

	string getValueText(double value) {
		return translate(this->values[(int)value].first);
	}

	void setValue(double value) {
		const char* valueStr = this->values[(int)value].second;
		this->source._SetNamedConfigParm(this->source.obj, this->source.fx,
			this->name.c_str(), valueStr);
	}

	const string getDisplayName() {
		return this->displayName;
	}
};

const FxNamedConfigParamValues TOGGLE_FX_NAMED_CONFIG_PARAM_VALUES = {
	{_t("off"), "0"},
	{_t("on"), "1"},
};
const FxNamedConfigParamValues REAEQ_BAND_TYPE_VALUES = {
	{_t("low shelf"), "0"},
	{_t("high shelf"), "1"},
	{_t("band"), "8"},
	{_t("low pass"), "3"},
	{_t("high pass"), "4"},
	{_t("all pass"), "5"},
	{_t("notch"), "6"},
	{_t("band pass"), "7"},
	{_t("parallel band pass"), "10"},
	{_t("band (alt)"), "9"},
	{_t("band (alt 2)"), "2"},
};

template<typename ReaperObj>
void FxParams<ReaperObj>::initNamedConfigParams() {
	char fxName[50];
	this->_GetFXName(this->obj, this->fx, fxName, sizeof(fxName));
	if (strcmp(fxName, "VST: ReaEQ (Cockos)") == 0) {
		for (int band = 0; ; ++band) {
			ostringstream name;
			name << "BANDENABLED" << band;
			char type[2];
			if (!this->_GetNamedConfigParm(this->obj, this->fx, name.str().c_str(),
					type, sizeof(type))) {
				// This band doesn't exist.
				break;
			}
			// Translators: A parameter in the FX Parameters dialog which adjusts
			// whether a ReaEQ band is enabled. {} will be replaced with the band
			// number; e.g. "band 2 enable".
			string dispName = format(translate("Band {} enable"), band + 1);
			this->namedConfigParams.push_back(FxNamedConfigParam(*this, dispName,
				name.str(), TOGGLE_FX_NAMED_CONFIG_PARAM_VALUES));
			name.str("");
			name << "BANDTYPE" << band;
			// Translators: A parameter in the FX Parameters dialog which adjusts
			// the type of a ReaEQ band. {} will be replaced with the band number;
			// e.g. "band 2 type".
			dispName = format(translate("Band {} type"), band + 1);
			this->namedConfigParams.push_back(FxNamedConfigParam(*this, dispName,
				name.str(), REAEQ_BAND_TYPE_VALUES));
		}
	}
}

template<typename ReaperObj>
unique_ptr<Param> FxParams<ReaperObj>::getParam(int fx, int param) {
	return make_unique<FxParam<ReaperObj>>(*this, fx, param);
}

class TrackParamProvider: public ReaperObjParamProvider {
	public:
	TrackParamProvider(const string displayName, MediaTrack* track,
		const string name, MakeParamFromProviderFunc makeParamFromProvider):
		ReaperObjParamProvider(displayName, name, makeParamFromProvider),
		track(track) {}

	virtual void* getSetValue(void* newValue) override {
		return GetSetMediaTrackInfo(this->track, this->name.c_str(), newValue);
	}

	private:
	MediaTrack* track;
};

class TrackSendParamProvider: public ReaperObjParamProvider {
	public:
	TrackSendParamProvider(const string displayName, MediaTrack* track,
		int category, int index, const string name,
		MakeParamFromProviderFunc makeParamFromProvider):
		ReaperObjParamProvider(displayName, name, makeParamFromProvider),
		track(track), category(category), index(index) {}

	virtual void* getSetValue(void* newValue) override {
		return GetSetTrackSendInfo(this->track, this->category, this->index,
			name.c_str(), newValue);
	}

	private:
	MediaTrack* track;
	int category;
	int index;
};

class TcpFxParamProvider: public ParamProvider {
	public:
	TcpFxParamProvider(const string displayName, FxParams<MediaTrack>& source,
		int fx, int param):
		ParamProvider(displayName), source(source), fx(fx), param(param) {}

	unique_ptr<Param> makeParam() {
		return this->source.getParam(this->fx, this->param);
	}

	private:
	FxParams<MediaTrack>& source;
	int fx;
	int param;
};

class TrackParams: public ReaperObjParamSource {
	private:
	MediaTrack* track;
	unique_ptr<FxParams<MediaTrack>> fxParams;

	void addSendParams(int category, const char* categoryName, const char* trackParam) {
		int count = GetTrackNumSends(track, category);
		for (int i = 0; i < count; ++i) {
			MediaTrack* sendTrack = (MediaTrack*)GetSetTrackSendInfo(this->track, category, i, trackParam, NULL);
			ostringstream dispPrefix;
			// Example display name: "1 Drums send volume"
			dispPrefix << (int)(size_t)GetSetMediaTrackInfo(sendTrack, "IP_TRACKNUMBER", NULL) << " ";
			char* trackName = (char*)GetSetMediaTrackInfo(sendTrack, "P_NAME", NULL);
			if (trackName)
				dispPrefix << trackName << " ";
			dispPrefix << categoryName << " ";
			this->params.push_back(make_unique<TrackSendParamProvider>(
				dispPrefix.str() + translate("volume"), this->track, category, i, "D_VOL",
				ReaperObjVolParam::make));
			this->params.push_back(make_unique<TrackSendParamProvider>(
				dispPrefix.str() + translate("pan"), this->track, category, i, "D_PAN",
				ReaperObjPanParam::make));
			this->params.push_back(make_unique<TrackSendParamProvider>(
				dispPrefix.str() + translate("mute"), this->track, category, i, "B_MUTE",
				ReaperObjToggleParam::make));
			this->params.push_back(make_unique<TrackSendParamProvider>(
				dispPrefix.str() + translate("mono"), this->track, category, i, "B_MONO",
				ReaperObjToggleParam::make));
		}
	}

	public:
	TrackParams(MediaTrack* track): track(track) {
		this->params.push_back(make_unique<TrackParamProvider>(translate("volume"),
			track, "D_VOL", ReaperObjVolParam::make));
		this->params.push_back(make_unique<TrackParamProvider>(translate("pan"),
			track, "D_PAN", ReaperObjPanParam::make));
		this->params.push_back(make_unique<TrackParamProvider>(translate("mute"),
			track, "B_MUTE", ReaperObjToggleParam::make));
		// Translators: Indicates a parameter for a track send in the Track Parameters
		// dialog.
		this->addSendParams(0, translate("send"), "P_DESTTRACK");
		// Translators: Indicates a parameter for a track receive in the Track
		// Parameters dialog.
		this->addSendParams(-1, translate("receive"), "P_SRCTRACK");

		int fxParamCount = CountTCPFXParms(nullptr, track);
		if (fxParamCount > 0) {
			this->fxParams = make_unique<FxParams<MediaTrack>>(track, "TrackFX");
			for (int i = 0; i < fxParamCount; ++i) {
				int fx, param;
				GetTCPFXParm(nullptr, track, i, &fx, &param);
				ostringstream displayName;
				char name[256];
				TrackFX_GetParamName(track, fx, param, name, sizeof(name));
				displayName << name;
				TrackFX_GetFXName(track, fx, name, sizeof(name));
				displayName << " (" << name << ")";
				this->params.push_back(make_unique<TcpFxParamProvider>(displayName.str(),
					*this->fxParams, fx, param));
			}
		}
	}

	string getTitle() {
		return translate("Track Parameters");
	}
};

class ItemParamProvider: public ReaperObjParamProvider {
	public:
	ItemParamProvider(const string displayName, MediaItem* item,
		const string name, MakeParamFromProviderFunc makeParamFromProvider):
		ReaperObjParamProvider(displayName, name, makeParamFromProvider),
		item(item) {}

	virtual void* getSetValue(void* newValue) override {
		return GetSetMediaItemInfo(this->item, this->name.c_str(), newValue);
	}

	private:
	MediaItem* item;
};

class TakeParamProvider: public ReaperObjParamProvider {
	public:
	TakeParamProvider(const string displayName, MediaItem_Take* take,
		const string name, MakeParamFromProviderFunc makeParamFromProvider):
		ReaperObjParamProvider(displayName, name, makeParamFromProvider),
		take(take) {}

	virtual void* getSetValue(void* newValue) override {
		return GetSetMediaItemTakeInfo(this->take, this->name.c_str(), newValue);
	}

	private:
	MediaItem_Take* take;
};

class ItemParams: public ReaperObjParamSource {
	public:
	ItemParams(MediaItem* item) {
		this->params.push_back(make_unique<ItemParamProvider>(
			translate("item volume"), item, "D_VOL", ReaperObjVolParam::make));
		// #74: Only add take parameters if there *is* a take. There isn't for empty items.
		if (MediaItem_Take* take = GetActiveTake(item)) {
			this->params.push_back(make_unique<TakeParamProvider>(
				translate("take volume"), take, "D_VOL", ReaperObjVolParam::make));
			this->params.push_back(make_unique<TakeParamProvider>(
				translate("take pan"), take, "D_PAN", ReaperObjPanParam::make));
		}
		this->params.push_back(make_unique<ItemParamProvider>(translate("mute"),
			item, "B_MUTE", ReaperObjToggleParam::make));
		this->params.push_back(make_unique<ItemParamProvider>(
			translate("fade in length"), item, "D_FADEINLEN",
			ReaperObjLenParam::make));
		this->params.push_back(make_unique<ItemParamProvider>(
			translate("Fade out length"), item, "D_FADEOUTLEN",
			ReaperObjLenParam::make));
	}

	string getTitle() {
		return translate("Item Parameters");
	}
};

void cmdParamsFocus(Command* command) {
	unique_ptr<ParamSource> source;
	MediaTrack* track;
	MediaItem_Take* take;
	int fx;
	if (getFocusedFx(&track, &take, &fx)) {
		if (take) {
			source = make_unique<FxParams<MediaItem_Take>>(take, "TakeFX", fx);
		} else {
			source = make_unique<FxParams<MediaTrack>>(track, "TrackFX", fx);
		}
		new ParamsDialog(std::move(source));
		return;
	}

	switch (fakeFocus) {
		case FOCUS_TRACK: {
			MediaTrack* track = GetLastTouchedTrack();
			if (!track)
				return;
			source = make_unique<TrackParams>(track);
			break;
		}
		case FOCUS_ITEM: {
			MediaItem* item = GetSelectedMediaItem(0, 0);
			if (!item)
				return;
			source = make_unique<ItemParams>(item);
			break;
		}
		default:
			return;
	}
	new ParamsDialog(std::move(source));
}

// Iterates through effects, including effects in containers.
template<typename ReaperObj>
class FxIterator {
	public:
	FxIterator(ReaperObj obj): obj(obj) {
		StackItem item;
		// The first call to next() should move to the first effect, index 0.
		item.indexInContainer = -1;
		if constexpr(is_same_v<ReaperObj, MediaTrack*>) {
			item.containerCount = TrackFX_GetCount(obj);
		} else {
			item.containerCount = TakeFX_GetCount(obj);
		}
		this->stack.push_back(item);
	}

	bool next() {
		StackItem* current = &this->stack.back();
		if (this->containedCount) {
			// This is a container. Enter it.
			StackItem sub;
			if (this->stack.size() == 1) {
				// This is a top level container.
				sub.containerFxIndex = 0x2000000 + current->indexInContainer + 1;
			} else {
				sub.containerFxIndex = this->fxIndex;
			}
			sub.multiplier = current->multiplier * (current->containerCount + 1);
			sub.containerCount = this->containedCount;
			this->stack.push_back(sub);
			return this->success();
		}
		for (;;) {
			// Get the next effect.
			++current->indexInContainer;
			if (current->indexInContainer < current->containerCount) {
				return this->success();
			}
			// We've reached the end of this container. Walk out of it.
			this->stack.pop_back();
			if (this->stack.empty()) {
				// There are no more effects of this type.
				break;
			}
			current = &this->stack.back();
		}
		if constexpr(is_same_v<ReaperObj, MediaTrack*>) {
			if (!this->rec) {
				// There might be input or monitoring effects.
				return this->firstRec();
			}
		}
		// There are no more effects.
		return false;
	}

	int getFxIndex() {
		return this->fxIndex;
	}

	bool isContainer() {
		return !!this->containedCount;
	}

	string getName() {
		char name[256];
		if constexpr (is_same_v<ReaperObj, MediaTrack*>) {
			TrackFX_GetFXName(obj, this->fxIndex, name, sizeof(name));
		} else {
			TakeFX_GetFXName(obj, this->fxIndex, name, sizeof(name));
		}
		ostringstream s;
		s << (this->stack.back().indexInContainer + 1) << " ";
		shortenFxName(name, s);
		if constexpr (is_same_v<ReaperObj, MediaTrack*>) {
			if (rec && this->stack.size() == 1) {
				s << " ";
				if (obj == GetMasterTrack(nullptr)) {
					// Translators: In the menu of effects when opening the FX Parameters
					// dialog, this is presented after effects which are monitoring FX.
					s << translate("[monitor]");
				} else {
					// Translators: In the menu of effects when opening the FX Parameters
					// dialog, this is presented after effects which are input FX.
					s << translate("[input]");
				}
			}
		}
		return s.str();
	}

	int getLevel() {
		return this->stack.size();
	}

	private:
	// Called when we successfully iterate to the next effect.
	bool success() {
		// Cache the index for this effect.
		this->fxIndex = this->rec ? 0x1000000 : 0;
		const StackItem& item = this->stack.back();
		if (this->stack.size() == 1) {
			// We're not in a container.
			this->fxIndex += item.indexInContainer;
		} else {
			this->fxIndex +=
				(item.indexInContainer + 1) * item.multiplier + item.containerFxIndex;
		}
		// If this is a container, cache how many effects it contains.
		char res[5] = "0";
		if constexpr (is_same_v<ReaperObj, MediaTrack*>) {
			TrackFX_GetNamedConfigParm(obj, this->fxIndex, "container_count", res,
				sizeof(res));
		} else {
			TakeFX_GetNamedConfigParm(obj, this->fxIndex, "container_count", res,
				sizeof(res));
		}
		this->containedCount = atoi(res);
		return true;
	}

	// Iterate to the first input or monitoring effect, if any.
	bool firstRec() {
		int count = TrackFX_GetRecCount(obj);
		if (count > 0) {
			this->rec = true;
			StackItem item;
			item.indexInContainer = 0;
			item.containerCount = count;
			this->stack.push_back(item);
			return this->success();
		}
		return false;
	}

	ReaperObj obj;
	bool rec = false;
	int fxIndex = -1;
	int containedCount = 0;

	struct StackItem {
		int indexInContainer = 0;
		int containerCount = 0;
		int containerFxIndex = 0;
		int multiplier = 1;
	};
	vector<StackItem> stack;
};

template<typename ReaperObj>
void fxParams_begin(ReaperObj* obj, const string& apiPrefix) {
	FxIterator iter(obj);
	int fx = -1;
	// Present a menu of effects.
	// We might have sub-menus, so we need a stack.
	vector<HMENU> menus;
	menus.push_back(CreatePopupMenu());
	MENUITEMINFO itemInfo;
	itemInfo.cbSize = sizeof(MENUITEMINFO);
	int count = 0;
	while (iter.next()) {
		// If we've exited containers, move to the appropriate ancestor menu.
		for (int level = menus.size(); level > iter.getLevel(); --level) {
			menus.pop_back();
		}
		itemInfo.fMask = MIIM_TYPE;
		itemInfo.fType = MFT_STRING;
		// Make sure this stays around until the InsertMenuItem call.
		const string name = iter.getName();
		itemInfo.dwTypeData = (char*)name.c_str();
		itemInfo.cch = name.length();
		fx = iter.getFxIndex();
		if (iter.isContainer()) {
			// Create a sub-menu for this container.
			itemInfo.fMask |= MIIM_SUBMENU;
			HMENU subMenu = CreatePopupMenu();
			itemInfo.hSubMenu = subMenu;
			InsertMenuItem(menus.back(), (UINT)count, true, &itemInfo);
			menus.push_back(subMenu);
			// The first item in the sub-menu allows access to the parameters for the
			// container itself.
			itemInfo.fMask = MIIM_TYPE | MIIM_ID;
			itemInfo.fType = MFT_STRING;
			itemInfo.dwTypeData = (char*)translate("(Container Parameters)");
			itemInfo.cch = strlen(itemInfo.dwTypeData);
			// We add 1 to wID because 0 means cancelled.
			itemInfo.wID = fx + 1;
			InsertMenuItem(subMenu, 0, true, &itemInfo);
		} else {
			itemInfo.fMask |= MIIM_ID;
			// We add 1 to wID because 0 means cancelled.
			itemInfo.wID = fx + 1;
			InsertMenuItem(menus.back(), (UINT)count, true, &itemInfo);
		}
		++count;
	}
	if (count == 0) {
		outputMessage(translate("no FX"));
		DestroyMenu(menus.front());
		return;
	}
	if (count > 1) {
		fx = TrackPopupMenu(menus.front(), TPM_NONOTIFY | TPM_RETURNCMD, 0, 0, 0,
			mainHwnd, nullptr) - 1;
		if (fx == -1) {
			return; // Cancelled.
		}
	}
	DestroyMenu(menus.front());

	auto source = make_unique<FxParams<ReaperObj>>(obj, apiPrefix, fx);
	new ParamsDialog(std::move(source));
}

void cmdFxParamsFocus(Command* command) {
	switch (fakeFocus) {
		case FOCUS_TRACK: {
			MediaTrack* track = GetLastTouchedTrack();
			if (!track)
				return;
			fxParams_begin(track, "TrackFX");
			break;
		}
		case FOCUS_ITEM: {
			MediaItem* item = GetSelectedMediaItem(0, 0);
			if (!item)
				return;
			MediaItem_Take* take = GetActiveTake(item);
			if (!take)
				return;
			fxParams_begin(take, "TakeFX");
			break;
		}
		default:
			break;
	}
}

void cmdFxParamsMaster(Command* command) {
	fxParams_begin(GetMasterTrack(0), "TrackFX");
}
