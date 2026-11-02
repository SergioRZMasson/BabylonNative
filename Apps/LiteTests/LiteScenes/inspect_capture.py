"""Inspect renderdoccmd's XML export; follow actual D3D11 object identities and bindings."""

import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def element(parent, name):
    if parent is None:
        return None
    return parent.find(f"*[@name='{name}']")


def value(parent, name, default=None):
    item = element(parent, name)
    if item is None or item.text is None:
        return default
    return item.get("string", item.text)


def number(parent, name, default=None):
    item = element(parent, name)
    return int(item.text) if item is not None and item.text is not None else default


def inspect(root):
    textures = {}
    views = {}
    samplers = {}
    rasterizers = {}
    bound_color = []
    bound_depth = None
    bound_samplers = {}
    bound_rasterizer = None
    topology = None
    draws = []
    for chunk in root.findall("chunks/chunk"):
        name = chunk.get("name", "")
        if name.endswith("CreateTexture2D") or name == "IDXGISwapChain::GetBuffer":
            descriptor = element(chunk, "Descriptor")
            if descriptor is None:
                descriptor = element(chunk, "BackbufferDescriptor")
            samples = element(descriptor, "SampleDesc")
            identity = number(chunk, "pTexture", number(chunk, "SwapbufferID"))
            textures[identity] = {
                "id": identity, "width": number(descriptor, "Width"),
                "height": number(descriptor, "Height"), "format": value(descriptor, "Format"),
                "samples": number(samples, "Count"), "mipLevels": number(descriptor, "MipLevels"),
            }
        elif name.endswith(("CreateRenderTargetView", "CreateDepthStencilView")):
            views[number(chunk, "pView")] = number(chunk, "pResource")
        elif name.endswith("CreateSamplerState"):
            descriptor = element(chunk, "Descriptor")
            if descriptor is None:
                descriptor = element(chunk, "pSamplerDesc")
            if descriptor is None:
                descriptor = element(chunk, "pDesc")
            identity = number(chunk, "pSamplerState", number(chunk, "pSampler", number(chunk, "pState")))
            samplers[identity] = {
                "id": identity, "maxAnisotropy": number(descriptor, "MaxAnisotropy"),
                "filter": value(descriptor, "Filter"), "minLod": value(descriptor, "MinLOD"),
                "maxLod": value(descriptor, "MaxLOD"),
            }
        elif name.endswith(("CreateRasterizerState", "CreateRasterizerState1", "CreateRasterizerState2")):
            descriptor = element(chunk, "Descriptor")
            if descriptor is None:
                descriptor = element(chunk, "pRasterizerDesc")
            if descriptor is None:
                descriptor = element(chunk, "pDesc")
            identity = number(chunk, "pRasterizerState", number(chunk, "pState"))
            rasterizers[identity] = {
                "id": identity, "frontCounterClockwise": value(descriptor, "FrontCounterClockwise"),
                "cullMode": value(descriptor, "CullMode"),
            }
        elif name.endswith(("OMSetRenderTargets", "OMSetRenderTargetsAndUnorderedAccessViews")):
            if number(chunk, "NumRTVs") == 4294967295:
                continue
            array = element(chunk, "ppRenderTargetViews")
            if array is not None:
                bound_color = [int(x.text) for x in array]
            bound_depth = number(chunk, "pDepthStencilView")
        elif name.endswith("PSSetSamplers"):
            array = element(chunk, "ppSamplers")
            slot = number(chunk, "StartSlot", 0)
            for index, item in enumerate(array):
                bound_samplers[slot + index] = int(item.text)
        elif name.endswith("RSSetState"):
            bound_rasterizer = number(chunk, "pRasterizerState")
        elif name.endswith("IASetPrimitiveTopology"):
            topology = value(chunk, "Topology")
        elif name.split("::")[-1] in ("Draw", "DrawIndexed", "DrawInstanced", "DrawIndexedInstanced"):
            draws.append({
                "chunk": number(chunk, "chunkIndex", int(chunk.get("chunkIndex"))),
                "call": name.split("::")[-1],
                "indices": number(chunk, "IndexCount", number(chunk, "IndexCountPerInstance")),
                "vertices": number(chunk, "VertexCount", number(chunk, "VertexCountPerInstance")),
                "instances": number(chunk, "InstanceCount", 1), "topology": topology,
                "color": [textures.get(views.get(view)) for view in bound_color if view],
                "depth": textures.get(views.get(bound_depth)),
                "samplers": [dict(samplers.get(identity, {"id": identity}), slot=slot)
                             for slot, identity in sorted(bound_samplers.items()) if identity],
                "rasterizer": rasterizers.get(bound_rasterizer),
            })
    return {"draws": draws, "textures": list(textures.values()),
            "samplers": list(samplers.values())}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xml", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = inspect(ET.parse(args.xml).getroot())
    result["xml"] = str(args.xml)
    result["xmlSha256"] = hashlib.sha256(args.xml.read_bytes()).hexdigest()
    if not result["draws"]:
        raise ValueError("No executed draw calls in capture")
    args.output.write_text(json.dumps(result, indent=2) + "\n")
