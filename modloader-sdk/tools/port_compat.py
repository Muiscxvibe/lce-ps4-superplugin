"""Static source file signals for features the importers do not translate."""
import re


JAVA_RESOURCES = (
    ('Shaders', r'(?:assets/[^/]+/)?shaders/.+',
     'Check renderer support in the target build before rewriting the render pass.'),
    ('OptiFine/MCPatcher resources', r'assets/[^/]+/(?:optifine|mcpatcher)/.+',
     'Port required texture or model variants individually with the target texture and block APIs.'),
    ('Vanilla texture replacements', r'assets/minecraft/textures/.+',
     'Identify the replaced vanilla keys and check the target texture override support.'),
    ('Bundled resource packs', r'(?:resourcepacks|resource_packs|builtin_resource_packs)/.+',
     'Inspect each bundled pack and port the assets it requires.'),
    ('Item definition assets', r'assets/[^/]+/items/.+\.json',
     'Map the item model references and any dynamic display rules to supported target item visuals.'),
    ('Dimensions and dimension types', r'data/[^/]+/(?:dimension|dimension_type)/.+\.json',
     'Check dimension backend support in the target build before porting terrain and travel logic.'),
    ('Terrain generation and world presets',
     r'data/[^/]+/worldgen/(?:world_preset|flat_level_generator_preset|noise|noise_settings|density_function|multi_noise_biome_source_parameter_list)/.+\.json',
     'Recreate the generator for the target world API. Preserve feature placement separately.'),
    ('Data pack functions', r'data/[^/]+/(?:function|functions)/.+\.mcfunction',
     'Rewrite required commands and triggers using the target Lua event and world APIs.'),
)

JAVA_CODE = (
    ('Java code', r'.+\.(?:class|java)',
     'Use lce_autoport.py for supported JAR patterns or reimplement the behaviour with Lua or the native API.'),
)

BEDROCK_BEHAVIOUR = (
    ('Bedrock scripts', r'(?:scripts/.+\.(?:js|mjs|ts)|manifest\.json \(script module\))',
     'Rewrite the Script API logic using the target Lua event and service APIs.'),
    ('Bedrock functions', r'functions/.+\.(?:mcfunction|json)',
     'Rewrite required commands and triggers using the target Lua event and world APIs.'),
    ('Entity spawn rules', r'spawn_rules/.+\.json',
     'Set spawn_weight, spawn_biomes and category in the entity hints and review the result.'),
    ('Feature placement rules', r'feature_rules/.+\.json',
     'Set the converted ore placement values in mod.json and review the target worldgen profile.'),
    ('Loot tables', r'loot_tables/.+\.json',
     'Map simple drops to block or entity definitions and implement remaining behaviour with events.'),
    ('Structures', r'structures/.+\.mcstructure',
     'Rebuild required templates in the SDK structure format and check its size limits.'),
    ('Bedrock dimensions', r'dimensions/.+\.json',
     'Check dimension backend support in the target build before porting terrain and travel logic.'),
)

BEDROCK_RESOURCES = (
    ('Bedrock shaders and materials', r'(?:shaders|materials)/.+',
     'Check renderer support in the target build before rewriting the render pass.'),
    ('Entity and block visuals',
     r'(?:entity|models|animations|animation_controllers|render_controllers|attachables)/.+\.json',
     'Review the target entity renderer and block shape capabilities and recreate required visuals.'),
    ('Interface layouts', r'ui/.+\.json',
     'Recreate required screens with the target UI API.'),
)


def diagnostics(names, rules, prefix=''):
    names = sorted(set(names))
    lines = []
    for label, pattern, action in rules:
        files = [prefix + name for name in names if re.fullmatch(pattern, name)]
        if not files:
            continue
        quoted = [repr(name).replace('`', r'\x60') for name in files[:3]]
        examples = ', '.join(f'`{name}`' for name in quoted)
        more = f' and {len(files) - 3} more' if len(files) > 3 else ''
        lines.append(f'**{label}** ({len(files)}). Found {examples}{more}. {action}')
    return lines


def java_features(names, *, data_only=False):
    rules = JAVA_RESOURCES + JAVA_CODE if data_only else JAVA_RESOURCES
    return diagnostics(names, rules)


def bedrock_features(behaviour, resources, manifest):
    if any(isinstance(module, dict) and module.get('type') in ('script', 'javascript')
           for module in manifest.get('modules') or []):
        behaviour = [*behaviour, 'manifest.json (script module)']
    return (diagnostics(behaviour, BEDROCK_BEHAVIOUR, 'BP/')
            + diagnostics(resources, BEDROCK_RESOURCES, 'RP/'))


def report_section(lines):
    return ['## Source features requiring manual work', '',
            'These files were detected by path or manifest metadata. Review them before publishing the port.', '',
            *[f'- {line}' for line in lines], ''] if lines else []
