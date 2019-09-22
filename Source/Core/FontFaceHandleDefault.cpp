/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "precompiled.h"
#include "FontFaceHandleDefault.h"
#include "FontDatabaseDefault.h"
#include <algorithm>
#include "../../Include/RmlUi/Core.h"
#include "FontFaceLayer.h"
#include "TextureLayout.h"

namespace Rml {
namespace Core {

#ifndef RMLUI_NO_FONT_INTERFACE_DEFAULT
	
FontFaceHandleDefault::FontFaceHandleDefault()
{
	metrics = {};
	base_layer = nullptr;
}

FontFaceHandleDefault::~FontFaceHandleDefault()
{
	glyphs.clear();
	layers.clear();
}

// Returns the point size of this font face.
int FontFaceHandleDefault::GetSize() const
{
	return metrics.size;
}

// Returns the average advance of all glyphs in this font face.
int FontFaceHandleDefault::GetCharacterWidth() const
{
	return metrics.average_advance;
}

// Returns the pixel height of a lower-case x in this font face.
int FontFaceHandleDefault::GetXHeight() const
{
	return metrics.x_height;
}

// Returns the default height between this font face's baselines.
int FontFaceHandleDefault::GetLineHeight() const
{
	return metrics.line_height;
}

// Returns the font's baseline.
int FontFaceHandleDefault::GetBaseline() const
{
	return metrics.baseline;
}

// Returns the font's glyphs.
const FontGlyphMap& FontFaceHandleDefault::GetGlyphs() const
{
	return glyphs;
}

float FontFaceHandleDefault::GetUnderline(float *thickness) const
{
	if (thickness != nullptr) {
		*thickness = metrics.underline_thickness;
	}
	return metrics.underline_position;
}

// Returns the width a string will take up if rendered with this handle.
int FontFaceHandleDefault::GetStringWidth(const String& string, CodePoint prior_character)
{
	int width = 0;
	for (auto it_string = StringIteratorU8(string); it_string; ++it_string)
	{
		CodePoint code_point = *it_string;

		const FontGlyph* glyph = GetOrAppendGlyph(code_point);
		if (!glyph)
			continue;

		// Adjust the cursor for the kerning between this character and the previous one.
		if (prior_character != CodePoint::Null)
			width += GetKerning(prior_character, code_point);
		// Adjust the cursor for this character's advance.
		width += glyph->advance;

		prior_character = code_point;
	}

	return width;
}

// Generates, if required, the layer configuration for a given array of font effects.
int FontFaceHandleDefault::GenerateLayerConfiguration(const FontEffectList& font_effects)
{
	if (font_effects.empty())
		return 0;

	// Check each existing configuration for a match with this arrangement of effects.
	int configuration_index = 1;
	for (; configuration_index < (int) layer_configurations.size(); ++configuration_index)
	{
		const LayerConfiguration& configuration = layer_configurations[configuration_index];

		// Check the size is correct. For a match, there should be one layer in the configuration
		// plus an extra for the base layer.
		if (configuration.size() != font_effects.size() + 1)
			continue;

		// Check through each layer, checking it was created by the same effect as the one we're
		// checking.
		size_t effect_index = 0;
		for (size_t i = 0; i < configuration.size(); ++i)
		{
			// Skip the base layer ...
			if (configuration[i]->GetFontEffect() == nullptr)
				continue;

			// If the ith layer's effect doesn't match the equivalent effect, then this
			// configuration can't match.
			if (configuration[i]->GetFontEffect() != font_effects[effect_index].get())
				break;

			// Check the next one ...
			++effect_index;
		}

		if (effect_index == font_effects.size())
			return configuration_index;
	}

	// No match, so we have to generate a new layer configuration.
	layer_configurations.push_back(LayerConfiguration());
	LayerConfiguration& layer_configuration = layer_configurations.back();

	bool added_base_layer = false;

	for (size_t i = 0; i < font_effects.size(); ++i)
	{
		if (!added_base_layer && font_effects[i]->GetLayer() == FontEffect::Layer::Front)
		{
			layer_configuration.push_back(base_layer);
			added_base_layer = true;
		}

		FontFaceLayer* new_layer = GetOrCreateLayer(font_effects[i]);
		layer_configuration.push_back(new_layer);
	}

	// Add the base layer now if we still haven't added it.
	if (!added_base_layer)
		layer_configuration.push_back(base_layer);

	return (int) (layer_configurations.size() - 1);
}

// Generates the texture data for a layer (for the texture database).
bool FontFaceHandleDefault::GenerateLayerTexture(UniquePtr<const byte[]>& texture_data, Vector2i& texture_dimensions, FontEffect* layer_id, int texture_id, int handle_version)
{
	if (handle_version != version)
		return false;

	FontLayerMap::iterator layer_iterator = layers.find(layer_id);
	if (layer_iterator == layers.end())
		return false;

	return layer_iterator->second->GenerateTexture(glyphs, texture_data, texture_dimensions, texture_id);
}

// Generates the geometry required to render a single line of text.
int FontFaceHandleDefault::GenerateString(GeometryList& geometry, const String& string, const Vector2f& position, const Colourb& colour, int layer_configuration_index)
{
	int geometry_index = 0;
	int line_width = 0;

	RMLUI_ASSERT(layer_configuration_index >= 0);
	RMLUI_ASSERT(layer_configuration_index < (int) layer_configurations.size());

	UpdateLayersOnDirty();

	// Fetch the requested configuration and generate the geometry for each one.
	const LayerConfiguration& layer_configuration = layer_configurations[layer_configuration_index];
	for (size_t i = 0; i < layer_configuration.size(); ++i)
	{
		FontFaceLayer* layer = layer_configuration[i];

		Colourb layer_colour;
		if (layer == base_layer)
			layer_colour = colour;
		else
			layer_colour = layer->GetColour();

		// Resize the geometry list if required.
		if ((int) geometry.size() < geometry_index + layer->GetNumTextures())
			geometry.resize(geometry_index + layer->GetNumTextures());
		
		RMLUI_ASSERT(!geometry.empty());

		// Bind the textures to the geometries.
		for (int i = 0; i < layer->GetNumTextures(); ++i)
			geometry[geometry_index + i].SetTexture(layer->GetTexture(i));

		line_width = 0;
		CodePoint prior_character = CodePoint::Null;

		geometry[geometry_index].GetIndices().reserve(string.size() * 6);
		geometry[geometry_index].GetVertices().reserve(string.size() * 4);

		for (auto it_string = StringIteratorU8(string); it_string; ++it_string)
		{
			CodePoint code_point = *it_string;

			const FontGlyph* glyph = GetOrAppendGlyph(code_point);
			if (!glyph)
				continue;

			// Adjust the cursor for the kerning between this character and the previous one.
			if (prior_character != CodePoint::Null)
				line_width += GetKerning(prior_character, code_point);

			layer->GenerateGeometry(&geometry[geometry_index], code_point, Vector2f(position.x + line_width, position.y), layer_colour);

			line_width += glyph->advance;
			prior_character = code_point;
		}

		geometry_index += layer->GetNumTextures();
	}

	// Cull any excess geometry from a previous generation.
	geometry.resize(geometry_index);

	return line_width;
}

bool FontFaceHandleDefault::UpdateLayersOnDirty()
{
	bool result = false;

	// If we are dirty, regenerate all the layers and increment the version
	if(is_layers_dirty && base_layer)
	{
		is_layers_dirty = false;
		++version;

		// Regenerate all the layers
		for (auto& pair : layers)
		{
			FontFaceLayer* layer = pair.second.get();
			GenerateLayer(layer);
		}

		result = true;
	}

	return result;
}

int FontFaceHandleDefault::GetVersion() const 
{
	return version;
}

FontGlyphMap& FontFaceHandleDefault::GetGlyphs() {
	return glyphs;
}

FontMetrics& FontFaceHandleDefault::GetMetrics() {
	return metrics;
}

void FontFaceHandleDefault::GenerateBaseLayer()
{
	RMLUI_ASSERTMSG(layer_configurations.empty(), "GenerateBaseLayer should only be called before any layers are generated.");
	base_layer = GetOrCreateLayer(nullptr);
	layer_configurations.push_back(LayerConfiguration{ base_layer });
}

const FontGlyph* FontFaceHandleDefault::GetOrAppendGlyph(CodePoint& code_point, bool look_in_fallback_fonts)
{
	// Don't try to render control characters
	if ((unsigned int)code_point < (unsigned int)' ')
		return nullptr;

	auto it_glyph = glyphs.find(code_point);
	if (it_glyph == glyphs.end())
	{
		bool result = AppendGlyph(code_point);

		if (result)
		{
			it_glyph = glyphs.find(code_point);
			if (it_glyph == glyphs.end())
			{
				RMLUI_ERROR;
				return nullptr;
			}

			is_layers_dirty = true;
		}
		else if (look_in_fallback_fonts)
		{
			const int num_fallback_faces = FontDatabaseDefault::CountFallbackFontFaces();
			for (int i = 0; i < num_fallback_faces; i++)
			{
				FontFaceHandleDefault* fallback_face = FontDatabaseDefault::GetFallbackFontFace(i, metrics.size).get();
				if (!fallback_face || fallback_face == this)
					continue;

				const FontGlyph* glyph = fallback_face->GetOrAppendGlyph(code_point, false);
				if (glyph)
				{
					// Insert the new glyph into our own set of glyphs
					auto pair = glyphs.emplace(code_point, glyph->WeakCopy());
					it_glyph = pair.first;
					if(pair.second)
						is_layers_dirty = true;
					break;
				}
			}

			// If we still have not found a glyph, use the replacement character.
			if(it_glyph == glyphs.end())
			{
				code_point = CodePoint::Replacement;
				it_glyph = glyphs.find(code_point);
				if (it_glyph == glyphs.end())
					return nullptr;
			}
		}
		else
		{
			return nullptr;
		}
	}

	const FontGlyph* glyph = &it_glyph->second;
	return glyph;
}

// Generates (or shares) a layer derived from a font effect.
FontFaceLayer* FontFaceHandleDefault::GetOrCreateLayer(const SharedPtr<const FontEffect>& font_effect)
{
	// Try inserting the font effect, it may have been instanced before as part of a different configuration.
	auto pair = layers.emplace(font_effect.get(), nullptr);

	bool inserted = pair.second;
	auto& layer = pair.first->second;

	if (!inserted)
		return layer.get();

	// The new effect was inserted, generate a new layer.
	layer = std::make_unique<FontFaceLayer>(font_effect);
	GenerateLayer(layer.get());

	return layer.get();
}

bool FontFaceHandleDefault::GenerateLayer(FontFaceLayer* layer)
{
	RMLUI_ASSERT(layer);
	const FontEffect* font_effect = layer->GetFontEffect();
	bool result = false;

	if (!font_effect)
	{
		result = layer->Generate(this);
	}
	else
	{
		// Determine which, if any, layer the new layer should copy its geometry and textures from.
		FontFaceLayer* clone = nullptr;
		bool clone_glyph_origins = true;
		String generation_key;
		size_t fingerprint = font_effect->GetFingerprint();

		if (!font_effect->HasUniqueTexture())
		{
			clone = base_layer;
			clone_glyph_origins = false;
		}
		else
		{
			auto cache_iterator = layer_cache.find(fingerprint);
			if (cache_iterator != layer_cache.end() && cache_iterator->second != layer)
				clone = cache_iterator->second;
		}

		// Create a new layer.
		result = layer->Generate(this, clone, clone_glyph_origins);

		// Cache the layer in the layer cache if it generated its own textures (ie, didn't clone).
		if (!clone)
			layer_cache[fingerprint] = layer;
	}

	return result;
}

#endif

}
}
