/*
    Copyright 2017 Nanoleaf Ltd.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include <cmath>

#include "AuroraPlugin.h"
#include "LayoutProcessingUtils.h"
#include "ColorUtils.h"
#include "DataManager.h"
#include "PluginFeatures.h"
#include "Logger.h"
#include "PluginOptionsManager.h"
#include "PluginOptions.h"

using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

	void initPlugin();
	void getPluginFrame(Frame_t* frames, int* nFrames, int* sleepTime);
	void pluginCleanup();

#ifdef __cplusplus
}
#endif

/* Constants */

const int MINIMUM_PANELS_COUNT = 3;

const int RED = 0;
const int YELLOW = 1;
const int GREEN = 2;

const int MAXIMUM_COLORS_COUNT = GREEN + 1;

const int IGNORED_PANEL_ID = -1;

/* Data */

LayoutData* layoutData = NULL;

RGB_t* paletteColors = NULL;
int paletteColorsCount = 0;

FrameSlice_t* frameSlices = NULL;
int frameSlicesCount = 0;

int transitionTime = 50;

/* Globals */

vector<RGB_t> colors(MINIMUM_PANELS_COUNT);
vector<int> colorPanelIds(MINIMUM_PANELS_COUNT);

int currentColorIndex = RED;

/**
 * @description: Initialize the plugin. Called once, when the plugin is loaded.
 * This function can be used to enable rhythm or advanced features,
 * e.g., to enable energy feature, simply call enableEnergy()
 * It can also be used to load the LayoutData and the colorPalette from the DataManager.
 * Any allocation, if done here, should be deallocated in the plugin cleanup function
 *
 */
void initPlugin() {
    /* Load data */

    layoutData = getLayoutData();

    getColorPalette(&paletteColors, &paletteColorsCount);
    rotateAuroraPanels(layoutData, &layoutData->globalOrientation);
    getFrameSlicesFromLayoutForTriangle(layoutData, &frameSlices, &frameSlicesCount, layoutData->globalOrientation);

    getOptionValue("transTime", transitionTime);

    /* Init default colors */

    colors[0] = {255, 0,   0};
    colors[1] = {255, 255, 0};
    colors[2] = {0,   255, 0};

    /* Load user colors */

    for (unsigned int paletteColorIndex = 0; paletteColorIndex < min(MINIMUM_PANELS_COUNT, paletteColorsCount); paletteColorIndex++) {
        colors[paletteColorIndex] = paletteColors[paletteColorIndex];
    }

    /* Identify middlest panels */

    int middleFrameSliceIndex = ceil((frameSlicesCount - 1) / 2);

    int minimumMiddleFrameSliceDistance = 31;

    int middlestFrameSliceIndex = -1;

    // Search 3 vertical panels as close to the middle as possible.
    for (unsigned int frameSliceIndex = 0; frameSliceIndex < frameSlicesCount; frameSliceIndex++) {
        FrameSlice_t frameSlice = frameSlices[frameSliceIndex];
        vector<int> panelIds = frameSlice.panelIds;
        int panelIdsCount = panelIds.size();

        int middleFrameSliceDistance = abs(middleFrameSliceIndex - (int)frameSliceIndex);

        if (panelIdsCount >= MINIMUM_PANELS_COUNT && middleFrameSliceDistance < minimumMiddleFrameSliceDistance) {
            minimumMiddleFrameSliceDistance = middleFrameSliceDistance;

            middlestFrameSliceIndex = frameSliceIndex;
        }
    }

    // Fallback to the panel(s) right in the middle.
    if (middlestFrameSliceIndex == -1) {
        middlestFrameSliceIndex = middleFrameSliceIndex;
    }

    // Get the middlest panel ids sorted by the vertical position.
    FrameSlice_t middlestFrameSlice = frameSlices[middlestFrameSliceIndex];
    vector<int> middlestPanelIds = middlestFrameSlice.panelIds;

    sort(middlestPanelIds.begin(), middlestPanelIds.end(), [](const int firstPanelId, const int secondPanelId) -> bool {
        double firstPanelY;
        double secondPanelY;

        for (unsigned int panelIdIndex = 0; panelIdIndex < layoutData->nPanels; panelIdIndex++) {
            Panel& panel = layoutData->panels[panelIdIndex];
            int panelId = panel.panelId;
            Shape* shape = panel.shape;
            double panelY = shape->getCentroid().y;

            if (panelId == firstPanelId) {
                firstPanelY = panelY;
            } else if (panelId == secondPanelId) {
                secondPanelY = panelY;
            }
        }
        return firstPanelY > secondPanelY;
    });

    int middlestPanelIdsCount = middlestPanelIds.size();

    if (middlestPanelIdsCount >= 3) {
        // Cycle between the 3 colors on 3 different panels.

        colorPanelIds[0] = middlestPanelIds[0];
        colorPanelIds[1] = middlestPanelIds[1];
        colorPanelIds[2] = middlestPanelIds[2];
    } else if (middlestPanelIdsCount == 2) {
        // Cycle between red and green on 2 different panels.

        colorPanelIds[0] = middlestPanelIds[0];
        colorPanelIds[1] = IGNORED_PANEL_ID;
        colorPanelIds[2] = middlestPanelIds[1];
    } else {
        // Cycle between the 3 colors on one panel.

        colorPanelIds[0] = middlestPanelIds[0];
        colorPanelIds[1] = middlestPanelIds[0];
        colorPanelIds[2] = middlestPanelIds[0];
    }
}

/**
 * @description: this the 'main' function that gives a frame to the Aurora to display onto the panels
 * To obtain updated values of enabled features, simply call get<feature_name>, e.g.,
 * getEnergy(), getIsBeat().
 *
 * If the plugin is a sound visualization plugin, the sleepTime variable will be NULL and is not required to be
 * filled in
 * This function, if is an effects plugin, can specify the interval it is to be called at through the sleepTime variable
 * if its a sound visualization plugin, this function is called at an interval of 50ms or more.
 *
 * @param frames: a pre-allocated buffer of the Frame_t structure to fill up with RGB values to show on panels.
 * Maximum size of this buffer is equal to the number of panels
 * @param nFrames: fill with the number of frames in frames
 * @param sleepTime: specify interval after which this function is called again, NULL if sound visualization plugin
 */
void getPluginFrame(Frame_t* frames, int* nFrames, int* sleepTime) {
    int index = 0;

    int redPanelAbsoluteFrameIndex = 0;
    int yellowPanelAbsoluteFrameIndex = 0;
    int greenPanelAbsoluteFrameIndex = 0;

    // Reset all the panels to black and find the target ones.
    for (unsigned int frameSliceIndex = 0; frameSliceIndex < frameSlicesCount; frameSliceIndex++) {
        FrameSlice_t frameSlice = frameSlices[frameSliceIndex];
        vector<int> panelIds = frameSlice.panelIds;
        int panelIdsCount = panelIds.size();

        for (unsigned int panelIdIndex = 0; panelIdIndex < panelIdsCount; panelIdIndex++) {
            int panelId = panelIds[panelIdIndex];

            frames[index].panelId = panelId;

            frames[index].r = 0;
            frames[index].g = 0;
            frames[index].b = 0;

            frames[index].transTime = 1;

            if (panelId == colorPanelIds[RED]) {
                redPanelAbsoluteFrameIndex = index;
            }

            if (panelId == colorPanelIds[YELLOW] && colorPanelIds[YELLOW] != IGNORED_PANEL_ID) {
                yellowPanelAbsoluteFrameIndex = index;
            }

            if (panelId == colorPanelIds[GREEN]) {
                greenPanelAbsoluteFrameIndex = index;
            }

            index++;
        }
    }

    // Set the current color for the respective panel.
    switch (currentColorIndex) {
        case RED: {
            RGB_t color = colors[RED];

            frames[redPanelAbsoluteFrameIndex].r = color.R;
            frames[redPanelAbsoluteFrameIndex].g = color.G;
            frames[redPanelAbsoluteFrameIndex].b = color.B;

            frames[redPanelAbsoluteFrameIndex].transTime = 1;

            *sleepTime = transitionTime;

            break;
        }
        case YELLOW: {
            RGB_t color = colors[YELLOW];

            frames[yellowPanelAbsoluteFrameIndex].r = color.R;
            frames[yellowPanelAbsoluteFrameIndex].g = color.G;
            frames[yellowPanelAbsoluteFrameIndex].b = color.B;

            frames[yellowPanelAbsoluteFrameIndex].transTime = 1;

            *sleepTime = transitionTime * 0.4;

            break;
        }
        case GREEN: {
            RGB_t color = colors[GREEN];

            frames[greenPanelAbsoluteFrameIndex].r = color.R;
            frames[greenPanelAbsoluteFrameIndex].g = color.G;
            frames[greenPanelAbsoluteFrameIndex].b = color.B;

            frames[greenPanelAbsoluteFrameIndex].transTime = 1;

            *sleepTime = transitionTime;

            break;
        }
    }

    // Set the next color.
    do {
        currentColorIndex++;

        if (currentColorIndex >= MAXIMUM_COLORS_COUNT) {
            currentColorIndex = RED;
        }
    } while (colorPanelIds[currentColorIndex] == IGNORED_PANEL_ID);

    *nFrames = index;
}

/**
 * @description: called once when the plugin is being closed.
 * Do all deallocation for memory allocated in initplugin here
 */
void pluginCleanup() {
    freeFrameSlices(frameSlices);
}
