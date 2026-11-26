#!/usr/bin/env python3
"""Parse MRP package headers and indexes into a Markdown report."""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass, field
from functools import lru_cache
from pathlib import Path
from urllib.parse import quote, urlsplit

if __package__:
    from .mrp_regression import read_pixels
else:
    from mrp_regression import read_pixels


HEADER_SIZE = 240
MAX_FILENAME_SIZE = 256
MAX_TRANSIENT_SCREEN_ADVANCES = 5
ENTRY_NOTICE_THRESHOLD = 0.9
SOUND_PROMPT_REGIONS = ((0, 145, 240, 175), (0, 295, 35, 320), (205, 295, 240, 320))
SOUND_PROMPT_MASKS = (
    "eNrtWNEOAyEIs///09uSXSIKgttd0tvap+kxQy2C2JogCIIg/C2wOeZy/g0zOGYwThz/abN5/51fLIQCYWaAUE+XPq++Vj6Pbyon2mBPr+/LRYR8zbwXzsYedME9qvX8"
    "BaPLim+0gYjPAl14H17CzUfL4WRPxnf2D/MRjPQdPqJavdjr6R7fKZsR5it4kod8V+E9RAUr39zfnq+fi+7E14nlgC+y3Msez+gU8/jOssGpO+l+MaUqLM6vQ9sUoJw"
    "v5eWqS0DRsS1enejz80W17cdpCoIgCIJw/QtB4c7GsMrue0f5iSDpugqN6GerjO1OoUMoXKqFGx29XDUU1qpYIDFIQs08lcZeIPcZxZ35qg06yeQUXwweJdoCEA==",
    "eJzt2NFuhCAQBVD4/59um1YcEDfpi4zknAcku2wyVxg3sRQAAAAAAAAAAAAAAAAAAAAAAAAAAIBt1anVVf3XPMXrktzHeFeQTXJ8ivGmIHLkIkcucuQiRy5y5CJHLnLkIkcucuQiRy675Njlvc82OXZ5L1p2eU8NtN6ddnIdruO8pOn72oY46TJNpvVYVOIPHiz7otuBOC0tVXcZPj+m9RhWCfvxXUdt1dzlKLXMoq7PcR6ZElN1RR6Luh46lp2b+XTt94bKaw1xLmv758LKGEO9sVem+3FdEzv9ycpH4VyERi7Xw993UliS4zT99Ha71+UcQ44av4hb8zf0adcYTkY47W0YcrTpZbayP34L6Po5PEHbHb/pjzz/5gAAAAAAAAAAAAAAAAAAAADwsC/TOwSN",
    "deRKVyKeJV9S6WxKyzySh7CXj6KhmZ+Mb6zvHJIr95W/sp1Oy/3r8OWCI+lhbPgG4c38kZVvW69YPs9XMn0dx5edLzoXgU6I5ykb48vOX5vCU+srxCTF8BbflHR5VDN"
    "GjnbJ49nbnfDT8wn6vuGhw2kWCoVCoVDIVJ/Iu/p9jbeoe2VR9ysrcD9Q3KpgrRWKrczeLWS0Yb234jeGdka03KsfCYtR9Q2n8K2tt/Y3tqGqgjUO9FjVdgFOYTiPUIt"
    "NrL+L9AkIWl1AXWKGnsEunSCcsmu6b1n5xFr+t1N8AI5bApo=",
    "eNrtWO0OwyAI5N7/pbcmXaeAQJpmweXuV7XGcB4foghBEARRAJLxMYW2xp+YBp8Z6AmfDbwte4uFpf2wjGBOaz6x1s4JIwcCvkZuzCeAhYM31vdggiVfIzI246v0fX9h"
    "0rjCF7INXyex4vqw+Woa2v9uemun7UDAhuRK3yA8musb19OYr1ET/fMVIvsNX1V/9DmcAYHOfE15MYKOfN0fGGtZZ421vjFf6BW6Gl/e0PTWgUExVy9J+Jp1Y4Jvmao"
    "QxK9DO4pfDC7fle6XwsJ+QyfwZxHZ6rpxu5b9M0eCIAiCIAjiiYtj/iqF/Gr5s12KbMrmVPvpwhPDvV10I1vo/QrtErFJ2OWKobBXZQWSBYmbIXsf8d6M7xAqnEqh2X1"
    "oySO2KLwAsIkB+g==",
    "eNrtWNGOwzAIw///0yddrxJgEtJNp1kafloWWuFADNRsMBgMBoPBYDAYDAaD1wHgmf12eb1RmyzoP+cx4rInLMz211nHhrhdK2wzIjyTj0aNfBFfXoHIwS3jfjBHne8y8f1"
    "LR2z45jMp9qX5gm7jLmDXz3C9i+1GzgQ43wTu7Mxy5Qz4hNw+sg3kiAYpdj47x7EIVyHdJHiSEk2nsLuQlT7bIr/l+CL5WwbU802KXNlDMqxlOAyF/Ea+0QBJ0BGPSK78G"
    "p7wBRmk+CI9A7nqa9QrhnbJKr5LA7gUMFmlApatojEdsmCN1qbLJLIBVs1E3WArzwufGDCrmaxtc//zLY9asraLOS78Xa08KC8HinwgYgf3/ulVGUjPFassfj+JaSBd5OjWh"
    "see8hsM+nLSXii0My963mcmZ758OX4AkjoBoA==",
)
SOUND_PROMPT_WIDE_REGIONS = ((0, 70, 240, 190), (0, 290, 35, 320), (205, 290, 240, 320))
SOUND_PROMPT_WIDE_MASKS = SOUND_PROMPT_MASKS[2:] + (
    "eNrt2dtugzAMAFD8/z+9aVorkjgXtDEF7ZynAmlq41xaehwAAAAAAAAAAAAAAAAAAAAAAAAAAACXxcXjvYL/Vhy8zkR94vWeo21+vr5/saJboGgziG490/T3rW9ZvizfaTmjOLVf3pHUqDws8y3OZ8M56l5j6/p+voqixqN8ezcwjm3zbdJ/RRnpejQ8bNpHbJVvHl89BTv1rS7G8va1+X56Ld/tN6dycerFe8p3NLyrUbFjdd/TdBjvOd98LXpSvslY7uQbs7V3828bX/Ua5duWLZJ9Z/D+2G6pisH8TdIuNqCH5Vsuqm282aiOfCI8ZDzftbf9l3wBAADgB7+i5w8NYv47+896WcxmOZzR9ZUmk4+Z91I/5csjHd+T9lkS3LFKzAdYLPS10iImDSazovgzsx9FzGOOxTsz7+H+Jr8Syy0+AJPJAhQ=",
    "eNrt2tFuwjAMBdDm/3962gZdarthexiy2nNeUCGg3Jg4lWDbAAAAAAAAAAAAAAAAAAAAAHiD8XC4ODxVDt9frwZ9D+uaN084RAkBPx9/RhQL1DdsrG9RrVzmY960YtUXonV95xnua7HImwoer7bRub5fFyOW6yxv2vFhddrlzfGfUx5z51nVN39fuuaN1UnZ9tyrBj5fFgO2bt0rbd5jmBf9qkp13v46hK1m+tv9W7/j8KEN86b2tMibt3NoeY+wTc/gP9e3al/xcR7T7fiNd4bF/p3XJJ3XIe98KI1ueZ9Nee6vVYr1cRzff3LrDQAAAAAAAFd1s1/GbvZL4LhX3nvF7f13yf+JO66bLP/58KJ5x4q4750AAMDDBxbCCnc=",
)
SOUND_PROMPT_WIDE_MASKS += (
    "eJzt3duOgyAQAFD5/5/e7rY1DAOiibtlk3MeKihSJlx8nG0DuKxcrC+lvITK+05pb7zf2XLz+vlfBnBRCZdRJUbQxFNVu+EvpTO6chDvdDpLuLVe3Hl+v0dYmgalaf4q9ZZz3b6zNj6tna1H6RnJfmP/CYVhGKV6tmC8jX1yfwr5PDqspvalDv7z+uNrt2Bd6R5Vk1lcJ96X4+/ntXiX/zjFw2k03ireo+XdrIpQWMS+TfdquDaLeyujs+g/xRur4Zo38/HZm9fHWko1Y8969zqIN0eV3l8r8Pip3dL8dMIOH6Buh+G6VLzxUK3u5G27/3amuu5x8fV8t7T/AQAAAAAAAAAAAAAAAAAAAAAAAAAAAACAgRNJDXKeqs/1MpHSEM2Gc/T8TJPJ38x7abOQdPuY5NbIuS7gTmd31ZlsIfMVWmbbJmWOG3cxatPLT9ZvePTwVFaYE4lUbmpyy1h+xRc2egIU",
)
SOUND_PROMPT_BLUE_REGIONS = ((0, 44, 240, 68), (85, 170, 160, 245), (0, 260, 240, 300))
SOUND_PROMPT_BLUE_MASKS = (
    "eJzt11EOgjAQQMHu/S+tMTEmImW3blTMzCchD0qhwBgAAAAAAACcWvSV2lLRlmorRXSlYiUVD5tty6WtL5XOmGq8Vj86wlpq1iqWJqlqab9VL+2kFkKvWmuZTWo989x6szT6SqtL3YlSQ+obqaZbtOsJbFsUNqHq62oSKrbuO78uraR2SqVWzEuVVhyU8qnbrrNSOhWdqaPx/ftXY630oVSx9PlPqlw7dU4xMr+pqdHlUqn1M5nKLOqR/nk+mrjr1sKUzm6AuKWypYPDtGQAAAAAAAAAAACAZxer4QQ5",
)
SOUND_PROMPT_GREEN_REGIONS = ((90, 135, 150, 190), (0, 300, 240, 320))
SOUND_PROMPT_GREEN_MASKS = (
    "eJztV0EOwyAMw///9DRpNE4a1mqVVsm1L4Q0BhxQCWMYAy1+Z55ir6nH5Avcb9QjsrnmmmvuE7h3/Z8v3SlL9hmmONpsoLTVHhSNcMU4jYkZlOemUZAGROePyd4m2hhu9sNjM1G52zpTTmZYyUnO2+IkdTnZ6y354RgkQR03PqBEYufJieAPvATWy+tJk7TzZot6OXqll0+VYRiGYRjCWBXiopXA6uUhWvlEmdf6tYCQVV4FknIHn9rPs2l2NOUWvXOXZeVOldSH8O7GhrJDWG5zHWnLbQXfuJp/QPwEN7DcR+AF+TwEsA==",
)
SPLASH_REGIONS = ((20, 30, 220, 270),)
SPLASH_MASKS = (
    "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",
)
SPLASH_MASKS += (
    "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",
)
SPLASH_MASKS += (
    "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",
)
SPLASH_MASKS += (
    "eJztm4tygyAQReH/f7ozUWCXhzHKvUrnHmcyTZrCHpZVURuCEEIIIYQQQgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEOIVxA9PR3GXmHg6kFtEw9OxXCZWPB3PVWqPRUUajUVFbPDFJG3r4DJgU5I+XEImeg87y0xOFlCpK8JWyTIqsclHcCZ+eylZIhV5qYiVNHwtfN5/EXlcp3dMqI8YPkxfHa+RGJdAO/L5O8PtAXpH6VaiCbrlUZU23sHJ1IHAl6w9oNELLFiNMzxxPDknsYuch50RG++RxI8auwovIy7giRJZhSTiQx5KXNOIvKlVRT1VgpmRNvDq9/c0vrRO8pgg8WmeoDL2mCQxbH++RreftOaYI4JWGXhMzMWgB4KGkZgng82I9zg8l70tAvcwGhGmAc2IG6nSHUQDmRHbPih4iohpHpYFowET8cO0rojxIOQDN7FS65FSHhGWkTJKS2vQPbAigVQdQJGYRXxnQA2ISPYgiaCun5hRQoZfPKATq/bC5gM3sQLtkA67ypiGaW987wwsAvPwjaM1oBOrXCFfMh9uj4WWQD4BkTsoHa2okfPBAS6yvAbTA3oTNHVB0KjvvwM8KBDy0e92FQ3mUtBr4I4ga2vwrpTUj3XM1uB4YDVoIqHdpkqQCj20DzhNBn6G+9EA5yNl5GsYK2jA89HVmKoSGSJ4DaOCF+kIzZbA7rHSMFUqUyFNLHSpR05GBjITif2MzNjR9j9F52Pa6cloqjb5mEqzJ/nlwo//Zv0sRK8daD7q5q/jtXga3afT59FOLXg+SjczORSZCiD4sQhuWhFE6pqB1gfpMhy6PjgK0PpgAp1WHAGsBvESHFykdIGXwWmELSW8Yl96YmEXtTSNorO8CPbokU3+QZl/RorzyCs+H3tPIIGOBvJuFPb0HavRDhxEAp8PvAdFw9wfpF6sBhHzK2gXzNEIoblykroHSEBFOhIzeSgf88LvSMBEOh3NpLSJzwb3FhtLASVDKvJZwT4qgsMOE6E6fLf3g29/Aj6EXDTsdfc5Ep1bdUiHIhKQy6m07iQwGNeLMZt325ozEvLRzcT17NiJldomeQQXxb0J5uottc3yCO56wzURv9eLzTKN4rHLbJ0eeHxX9Ps/+wuuxbHHaY32KEK3uLeQyiL2oEE6grhC798w/lmjCp3pceI2wrn6KOPhLy5RREK1+63Htxds+YZdLu0xN39EIxnkN4MpVa3xqm3/RrXj5Vnk+ZDeHq1N+gpGZG+RGL61SLkoIiOORfjhVyL1Z36HXC+BBxvsH4JP0RlFG1r1hOlBPrbvvoCj4A5L/G0i+dn3znQ5s71G42TIeUf96nyc0EgypfTTS1p7lJdnzH7Mh4k/bmuxkP4jzjT2WpFWYhv8F3mcUfkn+YhVJnL8u4+tjyfzcVLHbZ+/ecnOKmPH8JzCa/GnJItKbERXqyOBJVQs/nx2kVwU2tPx5RSEEEIIIYQQQgghhBBCCCGEEEJ85Q8EGywC",
)
LOADING_REGIONS = ((0, 70, 240, 250), (0, 285, 240, 320))
SPLASH_MASKS += (
    "eJztm4uyqyAMReP///Sd2xZIADkqIYnMXnNmjg/ELMNDbUsEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIjE0cM7qJt0HV7mMnR4icoVh/AqNyQCq9y2iCjyRCKex0OLYB6PLT4eUWQmLP4rBMnKzYtfH/Vb9Iu/Duiygjw21WAddxvJQ4mqDqOARyHMSJRKLOIdRzBlQf4iOhbeHloW5CuiqOHZ1VU1/OYQXQu3hPTintGI41Fte1bfklj/Pq2ahpNHtzPMpoPMPZZoRPCoNz6uUD/WK2fV8nh+Aaboakx45FZl69HXeOzRqcqGCx63b6wO+3nwROPhbC413uqRSzu0q0sal0IqRR+0x1mupeNCUJ0D4jWrv8LqlY7r0Y/spKS3R2/YrUtU/wNonHrMveON4HEhIfE0BuPS6z06N4rhNU6i1RAJ4zH7cU4Aj6N9tI2tMZ7F9vCYMbHWUJnvImgs8XDQWOHhobFAxEdD3cNJQ9vDS0NZxE9DU8TTQlHEWUPJw9uCdES8HT5sorGRyMueYwdsI/KuJ/Ihm4hMJiSMyCYesxpBROY1Yohs4qGhEUBER8PdQ0nDXWQTDzWNN3yxPb6HosY2r3628djlleI2Hru8qnb85GCHEUtVweMLyPoOSYV8v7Ok67KFhpHKegkbEyOP5Sp2HqtVdvGwMlksYaRiYrHaxWVm138I+f17J0ngu/xaDeFB7/Y4jh1EJBAJhn9ff3T+gz1EfbqKVULyECnWf3NYGwMfUHuXms9+vwpsRHJI/FdmZa36Bd3BDdiolAulQ8QZTCxOZmKqt50ewH+pRnmdSrty8qCbHkfjwRunTbPqOLSbxSXteneOKeUNNPjp6TymowrmYI2nraiyM3AQp69aB3cQglS/gKRyUyU9ZGI8PORW6dG8QmXjFfdqM7beQ3bscT7ybjYkiyWfObyJWKSj7CnFq/hZLS/yYC5EVaL4kdYafQ9pUo27xAvnXVLETyNH3M+HCLfq62U78/HUGHqIPJ145JcL1ioDj1Kg9aj7eyqW0+GTkjyRMQ++l0S8rBwxO77bwaOEla+ziP7Uo1jWGiV7xhr1+C+bW8ejam18p8+IJdt914PvPfFgD7KitKlHWWQbpBbXzV1pcP/k0K7aEEarAMyRuvLn79ORv12CWB8nNj6VmZCaFyae5GnuJ5GmyW83L/1dFmVlglBNd5QCzR5FIcWekhcqH1Ti45e9tCi5IymkMr6RM3K4dOLR9g/uEcgEAAAAAAAAAAAAAAAAAAAAAAAAAAAAsJB/LRUd5g==",
)
SPLASH_MASKS += (
    "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",
)
LOADING_REGIONS = ((0, 70, 240, 250), (0, 285, 240, 320))
SPLASH_MASKS = SPLASH_MASKS[:-4] + (
    "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",
)
SPLASH_MASKS = tuple(
    template.replace(
        "KIqiKIqiKIqiKIqiKIqiKIqiKIqiKIqi/D/5B2pDLec=",
        "KIqiKIqiKIqiKIqiKIqiKIqiKIqi/D/5B2pDLec=",
    )
    if template.startswith("eJzt24uS6iAQBNDw/")
    else template
    for template in SPLASH_MASKS
)
LOADING_MASKS = (
    "eNrtzwENAAAIA6Dbv7QtvjmhAQkAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABnTIWvry8AAAA8tLDxAZk=",
    "eNrt18sKgzAQBVDv//90aajmobgpLXR6DhgIZHOdMYnbBgAAAAAAAAAAwNfk9ezTXK6oljc3gQvlTfNMlMt4mYeC/bx2d1elumugMVm9+p7DZO3tOnnTv98e6CZvK30fTvMfKe+Ud8leK2+ONLls8KL9POTNtEWX2p7HEmY5jFL3/G31zXr2puL9am/U44aV+TUUvT9Pkc8pq+XNtFVvf/B/9O4KAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgA97AIMhALo=",
    "eNrt10tuwjAUBVDf/W+6VYW/iYFJiyudM0lAGHH9nh1TCgAAAAAAAAAAAAAAAAAAAADA+zJdHncZ3s7lszfjMw1uQ7+dErOb7ttPzpt5Sxtffsanjsl+yIdre1ff6T7jBL3qkqS1yJMBH87b67irdZapuPZKGZplqPNBYXOtaWqNSr081vImb7LUN7XHa31Pylt/z6WHc82+qW96fcta35QcWd+MSy3L9rz2+c2ayDBN83ccmTdrfTMs5s3+nLkdhiXbX+So/ep+/fa8N5vUfd70bW2t70kNvc+b8jJvr1qWfWC8plwfxR+agOWMMd2XdjIamvXZ+aplSt2/5iMLAADwT+VPHJv31/58yiuvvPLK63kEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAGx9AfiBAp8=",
)


@dataclass
class ResourceEntry:
    index: int
    name: str
    raw_name: bytes
    offset: int
    length: int
    reserved: int
    duplicate: bool = False


@dataclass
class PackageReport:
    path: Path
    file_size: int = 0
    metadata: dict[str, str | int] = field(default_factory=dict)
    entries: list[ResourceEntry] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    preview: str = ""
    preview_status: str = "not_captured"


def decode_text(value: bytes) -> str:
    value = value.split(b"\0", 1)[0].strip()
    for encoding in ("utf-8", "gbk"):
        try:
            return value.decode(encoding).strip()
        except UnicodeDecodeError:
            pass
    return value.decode("gbk", errors="replace").strip()


def read_u32(data: bytes, offset: int, byte_order: str = "<") -> int:
    return struct.unpack_from(f"{byte_order}I", data, offset)[0]


@lru_cache(maxsize=128)
def template_pixels(encoded: str) -> bytes:
    pixels = zlib.decompress(base64.b64decode(encoded, validate=True))
    return pixels.translate(bytes([0] + [1] * 255))


def read_catalog_png(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid catalog PNG signature")
    offset = 8
    compressed = bytearray()
    header = None
    ended = False
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        if offset + 12 + length > len(data):
            raise ValueError("Truncated catalog PNG")
        crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if zlib.crc32(kind + body) != crc:
            raise ValueError("Catalog PNG checksum mismatch")
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            compressed.extend(body)
        elif kind == b"IEND":
            ended = True
            break
        offset += length + 12
    if not ended or header != struct.pack(">IIBBBBB", 240, 320, 8, 2, 0, 0, 0):
        raise ValueError("Unsupported catalog PNG layout")
    decoder = zlib.decompressobj()
    rows = decoder.decompress(compressed, 320 * 721 + 1)
    if not decoder.eof or len(rows) != 320 * 721 or any(rows[y * 721] for y in range(320)):
        raise ValueError("Unsupported or corrupt catalog PNG rows")
    rgb = b"".join(rows[y * 721 + 1:(y + 1) * 721] for y in range(320))
    bgr = bytearray(len(rgb))
    bgr[0::3], bgr[1::3], bgr[2::3] = rgb[2::3], rgb[1::3], rgb[0::3]
    return bytes(bgr)


@lru_cache(maxsize=256)
def shifted_region_clip(width: int, height: int, offset_x: int) -> int:
    row = (
        b"\0" * offset_x + b"\xff" * (width - offset_x)
        if offset_x >= 0
        else b"\xff" * (width + offset_x) + b"\0" * -offset_x
    )
    return int.from_bytes(row * height, "little")


def image_mask_score(
    path: Path,
    regions: tuple[tuple[int, int, int, int], ...],
    templates: tuple[str, ...],
    tolerance_x: int = 0,
    tolerance_y: int = 0,
) -> float:
    try:
        data = path.read_bytes()
    except OSError:
        return 0.0
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        data = read_catalog_png(path)
        pixel_offset, width, signed_height, bits_per_pixel = 0, 240, -320, 24
    elif len(data) >= 30 and data[:2] == b"BM":
        pixel_offset = read_u32(data, 10)
        width, signed_height = struct.unpack_from("<ii", data, 18)
        bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    else:
        return 0.0
    height = abs(signed_height)
    if width != 240 or height != 320 or bits_per_pixel not in (24, 32):
        return 0.0
    stride = ((width * bits_per_pixel + 31) // 32) * 4

    masks: list[int] = []
    for x0, y0, x1, y1 in regions:
        region_mask = bytearray()
        for y in range(y0, y1):
            row = height - 1 - y if signed_height > 0 else y
            for x in range(x0, x1):
                pixel = pixel_offset + row * stride + x * (bits_per_pixel // 8)
                region_mask.append(max(data[pixel : pixel + 3]) > 60)
        masks.append(int.from_bytes(region_mask, "little"))

    best_score = 0.0
    expected_length = sum((x1 - x0) * (y1 - y0) for x0, y0, x1, y1 in regions)
    for encoded_template in templates:
        try:
            template = template_pixels(encoded_template)
        except (ValueError, zlib.error):
            continue
        if len(template) != expected_length:
            continue
        template_offset = 0
        intersection = 0
        union = 0
        dark_intersection = 0
        dark_union = 0
        for region_mask, (x0, y0, x1, y1) in zip(masks, regions):
            region_width = x1 - x0
            region_height = y1 - y0
            region_length = region_width * region_height
            template_region = template[template_offset : template_offset + region_length]
            template_offset += region_length
            template_mask = int.from_bytes(template_region, "little")

            if not tolerance_x and not tolerance_y:
                region_intersection = (region_mask & template_mask).bit_count()
                region_union = (region_mask | template_mask).bit_count()
            else:
                region_intersection = 0
                region_union = 0
                region_score = -1.0
                for offset_y in range(-tolerance_y, tolerance_y + 1):
                    for offset_x in range(-tolerance_x, tolerance_x + 1):
                        shift = (offset_y * region_width + offset_x) * 8
                        shifted = (template_mask << shift if shift >= 0 else template_mask >> -shift)
                        shifted &= shifted_region_clip(region_width, region_height, offset_x)
                        current_intersection = (region_mask & shifted).bit_count()
                        current_union = (region_mask | shifted).bit_count()
                        bright_score = current_intersection / current_union if current_union else 1.0
                        dark_score = (region_length - current_union) / (region_length - current_intersection) \
                            if region_length != current_intersection else 1.0
                        current_score = min(bright_score, dark_score)
                        if current_score > region_score:
                            region_score = current_score
                            region_intersection = current_intersection
                            region_union = current_union
            intersection += region_intersection
            union += region_union
            dark_intersection += region_length - region_union
            dark_union += region_length - region_intersection
        bright_score = intersection / union if union else 1.0
        dark_score = dark_intersection / dark_union if dark_union else 1.0
        best_score = max(best_score, min(bright_score, dark_score))
    return best_score


def sound_menu_score(path: Path) -> float:
    templates = (
        (((50, 65, 125, 90), (65, 90, 90, 125)),
         "eNpjZBwFo2AUjIJRMAqGGGCgHhgNzNHkM5oYhloMAgB5rQpb"),
        (((85, 115, 155, 140), (100, 140, 130, 175)),
         "eNpjZBwFo2AUjIJRMAooAQxUAfRwC1VNZgQjqrp+qMY5IxJJ9XAmIQbpkcJGwdBMsQD7VwoZ"),
        (((85, 115, 155, 140), (100, 140, 130, 175)),
         "eNrtlssWwyAIRC///9NdmCoQBNqcPhZh40F0HHBCFLntG0YwRRxhWQWjVjH9uZI1BNiI2zE5sQ6KUZ4exkVOLkVGZCjiUHZ1dGtyFAjIgJ+2LhiAhIpk+Y0y6qPjspz5bRLEEiGUCvpusZsMSszFj1YgaJUkXCw2SyDEKB0uKp2DFeqzatblOF9Vgx6XYBYnOJRemne06RGDEKF4b7vWmMuikt/QJ6L8kBXydpRMzanWkX5T/bu/elFnsm873cvm3VB3AV59jNTRVvshByY/FbmiukJYow8/AHqoCVw="),
        (((75, 135, 165, 180),),
         "eNrtl0sOgDAIRB/3v7QLF9oPogwmxpSVrXEyBctrzVb8NwjGGck2RmnnfcbzRAhhGYwDugmkdDhKFKV+F8Fk5dZyX1GEChK7ynruv3V+QTUbJwlKPJ+fBmVMVTYnG0ll3BnmS6lTJtmkuCjpLNslHbVkO6/4AgdzOyXk4NE3UD3PGMObHOSxdsRB8VAQcDCZimsOSsp3qFGj3BiW2B1zUPEctHpS0jEHLbdRYg6+T1iKOdic0Us5SOkVY4VSmQ0ubg5S"),
    )
    return max(image_mask_score(path, regions, (template,)) for regions, template in templates)


def sound_prompt_score(path: Path) -> float:
    observed_score = max(
        image_mask_score(path, ((55, 90, 185, 120), (0, 295, 35, 320), (205, 295, 240, 320)),
                         ("eNrtVUEOwyAMg/9/eurWsmCbEG6bZF8QbUgcy4TWDMMwjN9FvzFtpk8yfPxXQZ+wOgNOAcmh5LV+IwTlk/KogeiIpZgZUA9KtEMN4pnBLmFAouCu9TMN3puOLa0YkHOAb4EBE3qS9OipTAPWtM4AO6Bqg0l2WeJWBLS9L8kEc/qNE1WdtbF1eXW26gN9YkpaYkDGSxiwLcDMd/nyTDjWQBkT1xizHwc4X4UPIkuaH8AgXsm+Z/BcgOhllTcfD3h+8aQYhpE9RoZhGIbxz3gBRU0IuA==",)),
        image_mask_score(path, ((55, 145, 185, 175),),
                         ("eNrtVcsOwzAIi///p7dJixTAvDJthwmfmhRSG0O61mAw+A+guc6Oe0Ms9g70xs5ZNvx8fyMIrgjYM+FqpoQ6NZASGYNUMpaKv6jBKwkuA7HPTBDxKFiiFT2fILhHDDxJ8B0smLLzQDstXJr4lIHNgLXSq4F6iers9ue7x8D0aakTwcriMohMUZWrM8hPOBnwLvuMAXHAYYCsz/su4FDFGFhpIFOXKoibEEEfECJi/HIGxQvxaC3P/uJ1dzELX/pz//zDg8HgCg93fAGP",)),
        image_mask_score(path, ((55, 135, 185, 160),),
                         ("eNrtVFEOxSAIo/e/9Et8Cq3oxv5pFqMOsBTUrNFoZGDAJ746WK6PfyIWUNs6A3HgyWLDzOB/bN9AsvcEixqsBfsheIHMsyqad9hPdasaAP98aGeLSBmzUFTDrTYlBhGD+ZwZ2BBfqFwCMh3Y166YcdbR2gdw4cFV0EOV0bsAm5J+/lkDW004t0EtIWysJgHSRcjj1gfQNtNckm/hObCjZ4EBLk/ENw3AFuyZxkPpoyyasGrw0gd6GyJh8Khhh2pu55f4qkGj0Wg84Aez1AGH",)),
        image_mask_score(path, ((55, 150, 185, 180), (0, 295, 40, 320), (200, 295, 240, 320)),
                         ("eNrtV+0OwyAIhPd/6SXbusmXHNU1zQJ/Gi2VK3coErW1tbX9i3FxnC33NjE4ZlhPHE5k3cf3ZwA4AVmPNQS27x3vEgIgB2E8Z+LlX0PwnRpHQ2JnEfV7yRiOwGU1QOCRJPwROjSCYW1PafMcODpKhBny6KgsD0nhJygAgZz8LBdS8pzIanP2i8SZDmiuRHRzsDoqKHEzgtkK1yAQHwQIoqr7HAZ1BO5uZBG55wY7y5cRSMG7pGggRQSUA5BEGKkbLtyzaiEHhgsJXbOsPJhoMQc/7F4uD9zWdueuHmtZ0d721n483yfDjmh1vVP/wTvyAt5HzKGHHk3wAdJ25+JPaIJ1zpVLMKaNUt0QuAEkN1N9AYKqlfeWNdIkQt3s7rggH+PzAZLQBXw=",)),
        image_mask_score(path, ((85, 120, 155, 150), (0, 295, 40, 320), (200, 295, 240, 320)),
                         ("eNrtlkkOwzAIRXn3v3TVKYFgBivdVOIvIiOTD8hMIoPBDRCcvyKrG6N1Qglizy8C2r6gJftrTmJsPnUNJV6ldIk4DGTDl4ixZFn4frmhDskFcdjdimhB6Vmo3sgLuFev8gUJfGFN2yBR6tqJIyymWQwGg3tzlkaLzhs6rrX9TC+2a8d8a2Tt6HXjTXjRn1jPzpo43sX4lRkBg16hN/LFpnW6wHHJcIr2UWxQalN7/1LqhXyuLdCy+6nkqN5RB7J651zNaNV75J9Z8/5s5XsA+/cBYQ==",)),
        image_mask_score(path, ((70, 70, 170, 125), (0, 295, 40, 320), (200, 295, 240, 320)),
                         ("eNrtWNkOwjAMq///pwEBghxd3SyRhohfWHfUTXM0eIxGo3F94AUx+NzJ4RA/ziDbjvetbA6z9AcFSu24X6HCluEYkUUCqEB6TowCx2MxziCAZ1o2x9ekqLBF21HAgYFqDpMK+f6AmLjMH43GL7YLqfUXfsMBTM6RaCLCfoXpuoM9kORYLlsdhhyV4lBl0XLq8oWTHNOSPN/nZW9g9koObS9BRsDMDvUQbKSf5zAREfL50dY5ZxVthuDw/ZnHgVXMRPbKLg9OjB70J6DLH5y84zj4Cgs2hWNxFav73QM1Go3GWXWKVsquNF9I/dn7R8GoF6yScnY+rWiwqgSrLDT+KflJt4NXbceGuLt6iwliTineU3uxs4NJXWr6e8nrE7gBEf0CEA==",)),
        sound_menu_score(path),
    )
    if observed_score >= 0.8:
        return observed_score
    narrow_score = image_mask_score(
        path,
        SOUND_PROMPT_REGIONS,
        SOUND_PROMPT_MASKS[:2],
        tolerance_x=4,
        tolerance_y=10,
    )
    wide_score = image_mask_score(
        path,
        SOUND_PROMPT_WIDE_REGIONS,
        SOUND_PROMPT_WIDE_MASKS,
        tolerance_x=4,
        tolerance_y=6,
    )
    blue_score = image_mask_score(
        path,
        SOUND_PROMPT_BLUE_REGIONS,
        SOUND_PROMPT_BLUE_MASKS,
        tolerance_x=3,
        tolerance_y=4,
    )
    green_score = image_mask_score(
        path,
        SOUND_PROMPT_GREEN_REGIONS,
        SOUND_PROMPT_GREEN_MASKS,
        tolerance_x=3,
        tolerance_y=4,
    )
    return max(observed_score, narrow_score, wide_score, blue_score, green_score)


def splash_screen_score(path: Path) -> float:
    return image_mask_score(path, SPLASH_REGIONS, SPLASH_MASKS)


def loading_screen_score(path: Path) -> float:
    return image_mask_score(path, LOADING_REGIONS, LOADING_MASKS, tolerance_x=2, tolerance_y=3)


def transient_screen_score(path: Path) -> float:
    return max(splash_screen_score(path), loading_screen_score(path))


def entry_notice_score(path: Path) -> float:
    return image_mask_score(
        path,
        ((8, 45, 215, 61), (0, 300, 35, 320), (205, 300, 240, 320)),
        (
            "eNrdl4GOHDEIQ3n//9NVe7sbMIZJpdOpaqQ7aWcyTgDHJhFfg/gMvv74Pfrb1wvyax3kLyjjGW+az/ur10N0Sf/zjcN52neP7Llh2YCJazw03POeHPIEfV5z8lvywG11aJ/UNW/weh6IWOohv5DyJAjdGrGV5g8ZMPWesuPxiDHdndEkKuYtuLODlnaAz1h1AUqC7/B6qRLxtWCZiq8lJB9KNlxCbYk4BwEXzh0eAdNZpkdT+YCULvZwsBWWwgu7tnAMHh8Fa7s4WiDJSDrYV0ufq2ySMzYo2xbOBZ6ejKLU+vzzjxbdgT7kLuSBQbcs1Y0UXOF149BwSjSlgimCQRybKZRkxz7oWrvhTTJcyYXTXarHIpaG1309WVgtWLx6wxt9JIUDmsaxK8gdBV55LsKxPj8p2RpOMoxcJsYuIpQH7y+a0gZNRxvZylmghfmM58kmSUaW3njg+4FsfM3p1Emdx0pDu+K5glenkWzE7cC4/qps34/H32L+b6PqsqWiNpaPctwYnfq1VZLVtx62O/Yn45TjrO1WZRAYLbVPwVko43n2U3CS5hv4SNtnIiz95gb/NqV5HvMpNbdtR2Z3SxFz960yppnWy8G5uRDhG27yDWbpyd0VVLyNrucIa+xC9F69NrCnYdTe8cO+daGc0AC/UEj3MU4xp5vnKT/N9Avu/gKf+AQj",
            "eNrVl4FuwzAIRP3+/6enKrMD3OEk7TRt1lqtCbE5OOAyxrEYa3F8eK3jwlpju/D/z/3SLbK52eF4Lp0M8ytYBb8wG83nGfbESzSkdcJZfnzDcWhS1NDbejke8DKgO/4pnBwiUiIGNmTV7+yv8AHlCeuPsn9NXw/n6uqRW6Vqcg89msRBZsQT1cyhDKESZG6H2lF3uIEx186+TKK3BVuMKpyVYMowICJy2WSHps4a91J23M1S4Rk6voRdtQlbQ2LRTvQenK7GTXFJ7Uw+SOVxUibtVKtenP8QjqmVUuI11oQaogZ92+pMUkonXX5IGJ5kp/5C4QhFBh0JQ9RdRzyTSex/ie41X/fhaCPDp5r9+A3Nl9xTBE7Ij5sTuKzfbdTSacjkql7VO9q3sQ+mjk5LIDINZSQ+hcO4iEZNGzyBgxv65TQE0w6JeMXQDGXmGdGFDxppY5l+JJddbJtc7Meo6ZNn+58jktJckeGLtLsEABtZvGT4dOGLDO7MppsWP/fUf1qotBVVVfqxsWkrclQBvxtYKij37vYKqDPBaC0V3fiRy84EN3to69mb4Foa/mGapoQXgwH0nyY112vb7+o2jszuLQU/XrK2dGK6vhzMPapgrCYYpVt0AUafxYQbkyIokNfY8zZemK2YBEHrRBL7g7ImxB9URHBvYqqba5PfZvoN7n4BIsED9A==",
        ),
    )


def run_simulator_capture(
    simulator: Path,
    project_root: Path,
    package_path: Path,
    staged_package: Path,
    preview_path: Path,
    module_name: str,
    environment: dict[str, str],
    capture_delay_ms: int,
    timeout_seconds: int,
    click_after_ms: int | None = None,
    right_softkey_after_ms: int | None = None,
    sound_menu_after_ms: int | None = None,
) -> subprocess.CompletedProcess[str]:
    shutil.copy2(package_path, staged_package)
    command = [
        str(simulator),
        "--mrp",
        "capture.mrp",
        "--module",
        module_name,
        "--screenshot",
        str(preview_path.resolve()),
    ]
    if sum(value is not None for value in (click_after_ms, right_softkey_after_ms, sound_menu_after_ms)) > 1:
        raise ValueError("Choose one screenshot input action")
    if click_after_ms is not None:
        command.extend(["--click-right-after", str(click_after_ms)])
    elif right_softkey_after_ms is not None:
        command.extend(["--right-softkey-after", str(right_softkey_after_ms)])
    elif sound_menu_after_ms is not None:
        command.extend(["--sound-off-after", str(sound_menu_after_ms)])
    input_delay_ms = next((value for value in (click_after_ms, right_softkey_after_ms, sound_menu_after_ms)
                           if value is not None), 0)
    post_input_delay_ms = capture_delay_ms - (input_delay_ms or 0)
    if post_input_delay_ms < 0:
        raise ValueError("Screenshot time must not precede the input event")
    command.extend(["--capture-delay", str(post_input_delay_ms)])
    try:
        return subprocess.run(
            command,
            cwd=project_root,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(timeout_seconds, capture_delay_ms / 1000 + 10),
            check=False,
        )
    finally:
        staged_package.unlink(missing_ok=True)


def clear_report_output(output: Path, preserve_previews: bool = False) -> None:
    for name in ("index.md", "metadata.md", "issues.md", "resources", "previews"):
        if preserve_previews and name == "previews":
            continue
        path = output / name
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()


def capture_after_sound_prompt(
    simulator: Path,
    project_root: Path,
    package: PackageReport,
    staged_package: Path,
    preview_path: Path,
    module_name: str,
    environment: dict[str, str],
    wait_before_input_ms: int,
    capture_delay_ms: int,
    timeout_seconds: int,
    threshold: float,
) -> tuple[subprocess.CompletedProcess[str], float, bool]:
    completed = run_simulator_capture(
        simulator,
        project_root,
        package.path,
        staged_package,
        preview_path,
        module_name,
        environment,
        capture_delay_ms,
        timeout_seconds,
        click_after_ms=wait_before_input_ms,
    )
    click_score = sound_prompt_score(preview_path)
    if click_score < threshold:
        return completed, click_score, False

    click_preview = preview_path.read_bytes() if preview_path.is_file() else None
    preview_path.unlink(missing_ok=True)
    key_completed = run_simulator_capture(
        simulator,
        project_root,
        package.path,
        staged_package,
        preview_path,
        module_name,
        environment,
        capture_delay_ms,
        timeout_seconds,
        right_softkey_after_ms=wait_before_input_ms,
    )
    key_score = sound_prompt_score(preview_path)
    if preview_path.is_file() and (click_preview is None or key_score < click_score):
        return key_completed, key_score, True
    if click_preview is not None:
        preview_path.write_bytes(click_preview)
        return completed, click_score, False
    return key_completed, key_score, True


def parse_package(path: Path, data: bytes | None = None) -> PackageReport:
    report = PackageReport(path=path)
    if data is None:
        try:
            data = path.read_bytes()
        except OSError as exc:
            report.errors.append(f"Could not read package: {exc}")
            return report

    report.file_size = len(data)
    if len(data) < HEADER_SIZE:
        report.errors.append(f"Header is truncated ({len(data)} bytes; need {HEADER_SIZE})")
        return report
    if data[:4] != b"MRPG":
        report.errors.append(f"Invalid magic: {data[:4]!r}")
        return report

    header_end, declared_size, index_start = struct.unpack_from("<III", data, 4)
    appid_le, version_le = struct.unpack_from("<II", data, 68)
    appid, version = struct.unpack_from(">II", data, 192)
    report.metadata = {
        "Internal filename": decode_text(data[16:28]),
        "Application name": decode_text(data[28:52]),
        "APPID (big-endian fields)": appid,
        "Version (big-endian fields)": version,
        "Vendor": decode_text(data[88:128]),
        "Description": decode_text(data[128:192]),
        "Declared file size": declared_size,
        "Index start": index_start,
        "Index end": header_end + 8,
    }

    if (appid_le, version_le) != (appid, version):
        report.warnings.append(
            "Little-endian metadata at offsets 68/72 differs from the big-endian "
            "APPID/version fields at offsets 192/196; the latter are used."
        )
    if declared_size != len(data):
        difference = len(data) - declared_size
        if difference > 0:
            report.warnings.append(
                f"Physical file has {difference} trailing byte(s) beyond its declared size."
            )
        else:
            report.errors.append(
                f"Declared size ({declared_size}) exceeds physical size ({len(data)})."
            )

    index_end = header_end + 8
    if index_start < HEADER_SIZE or index_start > len(data):
        report.errors.append(f"Index start is outside the package: {index_start}")
        return report
    if index_end < index_start or index_end > len(data):
        report.errors.append(f"Index end is outside the package: {index_end}")
        return report
    if index_end > declared_size:
        report.errors.append(
            f"Index end ({index_end}) exceeds declared size ({declared_size})."
        )
        return report

    cursor = index_start
    seen_names: set[bytes] = set()
    while cursor < index_end:
        entry_start = cursor
        if index_end - cursor < 4:
            report.errors.append(f"Truncated filename length at index offset {cursor}.")
            break

        name_length = read_u32(data, cursor)
        cursor += 4
        if name_length < 1 or name_length >= MAX_FILENAME_SIZE:
            report.errors.append(
                f"Invalid filename length {name_length} at index offset {entry_start}."
            )
            break
        if cursor + name_length + 12 > index_end:
            report.errors.append(
                f"Truncated index entry at offset {entry_start} (name length {name_length})."
            )
            break

        encoded_name = data[cursor : cursor + name_length]
        raw_name = encoded_name.split(b"\0", 1)[0]
        cursor += name_length
        offset, length, reserved = struct.unpack_from("<III", data, cursor)
        cursor += 12
        if not raw_name:
            report.errors.append(f"Empty resource name at index offset {entry_start}.")
            break

        duplicate = raw_name.lower() in seen_names
        seen_names.add(raw_name.lower())
        end_offset = offset + length
        if offset < index_end:
            report.errors.append(
                f"Resource {decode_text(raw_name)!r} starts inside the index ({offset})."
            )
            break
        if end_offset > len(data) or end_offset > declared_size:
            report.errors.append(
                f"Resource {decode_text(raw_name)!r} range {offset}+{length} "
                f"exceeds package bounds."
            )
            break

        report.entries.append(
            ResourceEntry(
                index=len(report.entries) + 1,
                name=decode_text(raw_name),
                raw_name=raw_name,
                offset=offset,
                length=length,
                reserved=reserved,
                duplicate=duplicate,
            )
        )

    if cursor != index_end and not report.errors:
        report.errors.append(f"Index parsing stopped at {cursor}; expected {index_end}.")
    duplicate_count = sum(entry.duplicate for entry in report.entries)
    if duplicate_count:
        report.warnings.append(
            f"Contains {duplicate_count} duplicate resource name(s); runtime lookup "
            "returns the first matching entry."
        )
    return report


def capture_previews(
    packages: list[PackageReport],
    output: Path,
    simulator: Path,
    runtime_dir: Path,
    delay_ms: int,
    timeout_seconds: int,
    duplicate_preview_min: int = 0,
    skip_sound_prompt: bool = False,
    sound_prompt_threshold: float = 0.8,
    sound_prompt_wait_ms: int = 5000,
    post_skip_delay_ms: int = 5000,
    refresh_splash_screens: bool = False,
    splash_threshold: float = 0.9,
    splash_wait_ms: int = 7000,
    resume_existing: bool = False,
    headless: bool = False,
    show_progress: bool = True,
) -> int:
    if not simulator.is_file():
        raise FileNotFoundError(f"Simulator executable not found: {simulator}")
    if not (runtime_dir / "cfunction.ext").is_file():
        raise FileNotFoundError(f"Runtime cfunction.ext not found under: {runtime_dir}")
    if not (runtime_dir / "mythroad").is_dir():
        raise FileNotFoundError(f"Runtime mythroad directory not found under: {runtime_dir}")

    preview_dir = output / "previews"
    preview_dir.mkdir(parents=True, exist_ok=True)
    captured = 0
    project_root = Path(__file__).resolve().parent.parent

    with tempfile.TemporaryDirectory(prefix="vmrp-mrp-capture-") as temporary:
        staged_runtime = Path(temporary) / "runtime"
        shutil.copytree(runtime_dir, staged_runtime)
        staged_package = staged_runtime / "mythroad" / "capture.mrp"
        environment = os.environ.copy()
        environment["VMRP_RUNTIME_DIR"] = str(staged_runtime)
        environment["VMRP_DISABLE_NETWORK"] = "1"
        if headless:
            environment["SDL_VIDEODRIVER"] = "dummy"
            environment["SDL_AUDIODRIVER"] = "dummy"

        for number, package in enumerate(packages, 1):
            preview_name = package_report_name(number, package).removesuffix(".md") + ".bmp"
            preview_path = preview_dir / preview_name
            if package.errors:
                package.preview_status = "invalid_package"
                preview_path.unlink(missing_ok=True)
                package.warnings.append("Preview skipped because package parsing failed.")
                continue

            previous_preview = None
            if resume_existing and preview_path.is_file():
                with preview_path.open("rb") as image_file:
                    existing_header = image_file.read(30)
                if (
                    len(existing_header) >= 30
                    and existing_header[:2] == b"BM"
                    and struct.unpack_from("<ii", existing_header, 18) == (240, 320)
                    and entry_notice_score(preview_path) < ENTRY_NOTICE_THRESHOLD
                    and (not skip_sound_prompt or sound_prompt_score(preview_path) < sound_prompt_threshold)
                ):
                    package.preview = f"previews/{preview_name}"
                    package.preview_status = "candidate"
                    captured += 1
                    continue
            preview_path.unlink(missing_ok=True)
            package.preview_status = "capture_failed"

            try:
                module_name = next(
                    (
                        entry.name
                        for preferred in ("start.mr", "_start.mr")
                        for entry in package.entries
                        if entry.name.lower() == preferred
                    ),
                    next(
                        (entry.name for entry in package.entries if entry.name.lower().endswith(".mr")),
                        "start.mr",
                    ),
                )
                completed = run_simulator_capture(
                    simulator,
                    project_root,
                    package.path,
                    staged_package,
                    preview_path,
                    module_name,
                    environment,
                    delay_ms,
                    timeout_seconds,
                )

                startup_screen_score = 0.0
                startup_screen_advances = 0
                sound_score = 0.0
                sound_detected_score = 0.0
                sound_action = False
                used_key_fallback = False
                elapsed_ms = delay_ms
                input_at_ms = None
                sound_menu = False

                for _ in range(MAX_TRANSIENT_SCREEN_ADVANCES):
                    if not preview_path.is_file():
                        break
                    if entry_notice_score(preview_path) >= ENTRY_NOTICE_THRESHOLD:
                        break
                    if skip_sound_prompt:
                        sound_score = sound_prompt_score(preview_path)
                        if sound_score >= sound_prompt_threshold and input_at_ms is None:
                            sound_detected_score = max(sound_detected_score, sound_score)
                            sound_action = True
                            input_at_ms = max(sound_prompt_wait_ms, elapsed_ms + 200)
                            elapsed_ms = min(60000, input_at_ms + post_skip_delay_ms)
                            sound_menu = sound_menu_score(preview_path) >= sound_prompt_threshold
                            if sound_menu:
                                completed = run_simulator_capture(
                                    simulator, project_root, package.path, staged_package, preview_path,
                                    module_name, environment, elapsed_ms, timeout_seconds,
                                    sound_menu_after_ms=input_at_ms,
                                )
                                continue
                            completed, sound_score, used_key_fallback = capture_after_sound_prompt(
                                simulator,
                                project_root,
                                package,
                                staged_package,
                                preview_path,
                                module_name,
                                environment,
                                input_at_ms,
                                elapsed_ms,
                                timeout_seconds,
                                sound_prompt_threshold,
                            )
                            continue
                    if refresh_splash_screens:
                        current_startup_score = transient_screen_score(preview_path)
                        if current_startup_score >= splash_threshold:
                            startup_screen_score = max(
                                startup_screen_score, current_startup_score
                            )
                            startup_screen_advances += 1
                            next_elapsed_ms = min(60000, elapsed_ms + splash_wait_ms)
                            if next_elapsed_ms == elapsed_ms:
                                break
                            elapsed_ms = next_elapsed_ms
                            completed = run_simulator_capture(
                                simulator,
                                project_root,
                                package.path,
                                staged_package,
                                preview_path,
                                module_name,
                                environment,
                                elapsed_ms,
                                timeout_seconds,
                                click_after_ms=input_at_ms if not used_key_fallback and not sound_menu else None,
                                right_softkey_after_ms=input_at_ms if used_key_fallback and not sound_menu else None,
                                sound_menu_after_ms=input_at_ms if sound_menu else None,
                            )
                            continue
                    break

                remaining_startup_score = (
                    transient_screen_score(preview_path)
                    if refresh_splash_screens and preview_path.is_file()
                    else 0.0
                )
                sound_score = (
                    sound_prompt_score(preview_path)
                    if skip_sound_prompt and preview_path.is_file()
                    else 0.0
                )
                notice_score = entry_notice_score(preview_path) if preview_path.is_file() else 0.0
                if not preview_path.is_file():
                    detail = next(
                        (
                            line.strip()
                            for line in reversed((completed.stdout or "").splitlines())
                            if line.strip()
                        ),
                        f"exit code {completed.returncode}",
                    )
                    package.warnings.append(
                        f"Preview capture failed (exit code {completed.returncode}): {detail[:240]}"
                    )
                    if previous_preview is not None:
                        preview_path.write_bytes(previous_preview)
                        package.preview = f"previews/{preview_name}"
                        captured += 1
                        package.warnings.append("Retained the previous valid preview.")
                    continue

                with preview_path.open("rb") as image_file:
                    header = image_file.read(30)
                if len(header) < 30 or header[:2] != b"BM":
                    package.warnings.append("Preview output is not a valid BMP image.")
                    preview_path.unlink(missing_ok=True)
                    if previous_preview is not None:
                        preview_path.write_bytes(previous_preview)
                        package.preview = f"previews/{preview_name}"
                        captured += 1
                        package.warnings.append("Retained the previous valid preview.")
                    continue
                width, height = struct.unpack_from("<ii", header, 18)
                if (width, height) != (240, 320):
                    package.warnings.append(
                        f"Preview has unexpected dimensions: {width}x{height}."
                    )
                    preview_path.unlink(missing_ok=True)
                    if previous_preview is not None:
                        preview_path.write_bytes(previous_preview)
                        package.preview = f"previews/{preview_name}"
                        captured += 1
                        package.warnings.append("Retained the previous valid preview.")
                    continue
                if startup_screen_advances:
                    package.warnings.append(
                        "Splash/loading startup screen detected "
                        f"(score {startup_screen_score:.3f}); "
                        f"advanced {startup_screen_advances} time(s) with {splash_wait_ms} ms waits."
                    )
                if remaining_startup_score >= splash_threshold:
                    package.warnings.append(
                        "Splash/loading screen remains after "
                        f"{MAX_TRANSIENT_SCREEN_ADVANCES} startup advances "
                        f"(score {remaining_startup_score:.3f})."
                    )
                if sound_action:
                    action = "Down + left-softkey sound-off selection" if sound_menu else (
                        "right-softkey fallback" if used_key_fallback else "lower-right No click")
                    package.warnings.append(
                        f"Sound confirmation prompt detected (score {sound_detected_score:.3f}); "
                        f"used {action} after {input_at_ms} ms; "
                        f"final prompt score {sound_score:.3f}."
                    )
                    if sound_score >= sound_prompt_threshold:
                        package.warnings.append("Sound confirmation prompt remains in the final preview.")
                if notice_score >= ENTRY_NOTICE_THRESHOLD:
                    package.warnings.append("Entry/fee notice withheld without confirming or entering.")
                if completed.returncode != 0:
                    package.warnings.append(
                        f"Simulator exited with code {completed.returncode} after saving the preview."
                    )
                    package.preview_status = "runtime_error"
                elif remaining_startup_score >= splash_threshold or sound_score >= sound_prompt_threshold \
                        or notice_score >= ENTRY_NOTICE_THRESHOLD:
                    package.preview_status = "startup_screen"
                else:
                    pixels = read_pixels(preview_path)
                    if pixels == pixels[:3] * (240 * 320):
                        package.warnings.append("Preview is a single-color screen.")
                        package.preview_status = "blank_screen"
                    else:
                        package.preview = f"previews/{preview_name}"
                        package.preview_status = "candidate"
                        captured += 1
                if not package.preview:
                    preview_path.unlink(missing_ok=True)
            except subprocess.TimeoutExpired:
                package.warnings.append(
                    f"Preview capture timed out after {timeout_seconds} seconds."
                )
                preview_path.unlink(missing_ok=True)
            except (OSError, ValueError) as exc:
                package.warnings.append(f"Preview capture failed: {exc}")
                preview_path.unlink(missing_ok=True)
            finally:
                staged_package.unlink(missing_ok=True)

            if show_progress and (number % 25 == 0 or number == len(packages)):
                print(f"Preview capture progress: {number}/{len(packages)} ({captured} saved)")

    if duplicate_preview_min:
        groups: dict[str, list[PackageReport]] = {}
        for package in packages:
            if package.preview:
                digest = hashlib.sha256((output / package.preview).read_bytes()).hexdigest()
                groups.setdefault(digest, []).append(package)
        for group in groups.values():
            if len(group) >= duplicate_preview_min:
                for package in group:
                    (output / package.preview).unlink(missing_ok=True)
                    package.preview = ""
                    package.preview_status = "shared_screen"
                    package.warnings.append("Shared identical preview withheld for review.")
                    captured -= 1
    return captured


def write_preview_png(bmp: Path, destination: Path) -> None:
    pixels = read_pixels(bmp)
    rgb = bytearray(len(pixels))
    rgb[0::3], rgb[1::3], rgb[2::3] = pixels[2::3], pixels[1::3], pixels[0::3]
    scanlines = b"".join(b"\0" + rgb[start:start + 720] for start in range(0, len(rgb), 720))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    destination.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 320, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, 9))
        + chunk(b"IEND", b"")
    )


@dataclass
class CatalogEntry:
    id: str
    filename: str
    name: str
    vendor: str
    description: str
    appid: int
    version: int
    size_bytes: int
    resource_count: int
    sha256: str
    download_url: str
    errors: list[str]
    warnings: list[str]
    preview: str | None = None
    capture_status: str = "pending"


def generate_catalog(source: Path, packages: list[PackageReport], output: Path, args: argparse.Namespace) -> None:
    base = args.download_base_url.rstrip("/") + "/"
    url = urlsplit(base)
    if url.scheme not in ("http", "https") or not url.netloc or url.query or url.fragment:
        raise ValueError("--download-base-url must be an absolute HTTP(S) directory URL without query or fragment")
    if url.netloc == "github.com":
        base = base.replace("/tree/", "/blob/", 1)
    source_url = base.replace("/blob/", "/tree/", 1).rstrip("/")
    relative_root = source if source.is_dir() else source.parent
    previews = output / "previews"
    previews.mkdir(parents=True, exist_ok=True)
    entries = []
    for package in packages:
        filename = package.path.relative_to(relative_root).as_posix()
        digest = hashlib.sha256(package.path.read_bytes()).hexdigest()
        entries.append(CatalogEntry(
            id=hashlib.sha256(filename.encode("utf-8")).hexdigest()[:16],
            filename=filename,
            name=str(package.metadata.get("Application name", "")) or package.path.stem,
            vendor=str(package.metadata.get("Vendor", "")),
            description=str(package.metadata.get("Description", "")),
            appid=int(package.metadata.get("APPID (big-endian fields)", 0)),
            version=int(package.metadata.get("Version (big-endian fields)", 0)),
            size_bytes=package.file_size,
            resource_count=len(package.entries),
            sha256=digest,
            download_url=base + quote(filename, safe="/"),
            errors=[error.replace(str(package.path), filename) for error in package.errors],
            warnings=list(package.warnings),
            capture_status="invalid_package" if package.errors else "pending",
        ))

    catalog_path = output / "catalog.json"
    if args.resume_capture and catalog_path.is_file():
        previous = json.loads(catalog_path.read_text(encoding="utf-8"))
        if previous.get("schema_version") != 1 or previous.get("source_url") != source_url \
                or previous.get("capture_policy", {}).get("matcher_version") not in (2, 3, 4):
            raise ValueError("Existing catalog schema/source differs; regenerate without --resume-capture")
        old_entries = {entry["id"]: entry for entry in previous["packages"]}
        for entry in entries:
            old = old_entries.get(entry.id)
            expected = f"previews/{entry.sha256}.png"
            if old and old["sha256"] == entry.sha256 and old["capture_status"] == "candidate" \
                    and old["preview"] == expected and (output / expected).is_file():
                try:
                    read_catalog_png(output / expected)
                    if sound_prompt_score(output / expected) < args.sound_prompt_threshold \
                            and transient_screen_score(output / expected) < args.splash_threshold \
                            and entry_notice_score(output / expected) < ENTRY_NOTICE_THRESHOLD:
                        entry.preview = expected
                        entry.capture_status = "candidate"
                        entry.warnings = old.get("warnings", [])
                except (OSError, ValueError, zlib.error) as exc:
                    print(f"Recapturing {entry.filename}: {exc}", file=sys.stderr)

    review_path = output / "reviews.json"
    if review_path.is_file():
        reviews = json.loads(review_path.read_text(encoding="utf-8"))
        if reviews.get("schema_version") != 1 or not isinstance(reviews.get("withheld"), dict):
            raise ValueError("Invalid catalog reviews.json schema")
        for digest, reason in reviews["withheld"].items():
            if not re.fullmatch(r"[a-f0-9]{64}", digest) or reason not in ("audio_prompt", "startup_screen", "uncertain"):
                raise ValueError("Invalid catalog review hash or reason")
        for entry in entries:
            reason = reviews["withheld"].get(entry.sha256)
            if reason:
                entry.preview = None
                entry.capture_status = "review_required"
                entry.warnings.append(f"Preview withheld after contact-sheet review: {reason}.")

    def save() -> None:
        document = {
            "schema_version": 1,
            "source_url": source_url,
            "capture_policy": {
                "known_startup_screens_hidden": True,
                "matcher_version": 4,
                "audio_choice": "no",
                "automatic_previews_require_review": True,
            },
            "summary": {
                "total": len(entries),
                "structurally_valid": sum(not entry.errors for entry in entries),
                "previews": sum(entry.preview is not None for entry in entries),
                "pending": sum(entry.capture_status == "pending" for entry in entries),
                "review_withheld": sum(entry.capture_status == "review_required" for entry in entries),
            },
            "packages": [asdict(entry) for entry in entries],
        }
        temporary = catalog_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temporary.replace(catalog_path)

    def capture(index: int) -> tuple[int, str, str | None]:
        package = packages[index]
        with tempfile.TemporaryDirectory(prefix="mrpx-catalog-") as temporary:
            directory = Path(temporary)
            capture_previews(
                [package], directory, args.simulator.expanduser().resolve(),
                args.runtime_dir.expanduser().resolve() if args.runtime_dir
                else Path(__file__).resolve().parent.parent / "assets" / "runtime",
                args.capture_delay, args.capture_timeout,
                skip_sound_prompt=True, refresh_splash_screens=True,
                sound_prompt_threshold=args.sound_prompt_threshold,
                sound_prompt_wait_ms=args.sound_prompt_wait,
                post_skip_delay_ms=args.post_skip_delay,
                splash_threshold=args.splash_threshold,
                splash_wait_ms=args.splash_wait,
                headless=True, show_progress=False,
            )
            if package.preview:
                name = f"previews/{entries[index].sha256}.png"
                temporary_png = directory / "preview.png"
                write_preview_png(directory / package.preview, temporary_png)
                shutil.copyfile(temporary_png, output / name)
                return index, package.preview_status, name
            if package.warnings:
                print(f"{entries[index].filename}: {package.warnings[-1]}", file=sys.stderr)
            return index, package.preview_status, None

    save()
    if args.capture:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = [pool.submit(capture, index) for index, entry in enumerate(entries)
                       if entry.capture_status == "pending"]
            for number, future in enumerate(as_completed(futures), 1):
                index, status, preview = future.result()
                entries[index].capture_status = status
                entries[index].preview = preview
                entries[index].warnings = [
                    re.sub(r"(?:[A-Za-z]:[\\/]|/tmp/)[^\r\n]*", "<local-path>", warning)
                    for warning in packages[index].warnings
                ]
                if number % 25 == 0 or number == len(futures):
                    save()
                    print(f"Catalog capture: {number}/{len(futures)}; {sum(e.preview is not None for e in entries)} previews", flush=True)
    else:
        for entry in entries:
            if entry.capture_status == "pending":
                entry.capture_status = "not_captured"
    identical: dict[str, list[CatalogEntry]] = {}
    for entry in entries:
        if entry.preview:
            digest = hashlib.sha256((output / entry.preview).read_bytes()).hexdigest()
            identical.setdefault(digest, []).append(entry)
    for group in identical.values():
        if len({entry.sha256 for entry in group}) >= args.duplicate_preview_min:
            for entry in group:
                entry.capture_status = "shared_screen"
                entry.preview = None
    referenced = {entry.preview for entry in entries if entry.preview}
    for entry in entries:
        name = f"previews/{entry.sha256}.png"
        if name not in referenced:
            (output / name).unlink(missing_ok=True)
    for path in previews.glob("*.png"):
        if re.fullmatch(r"[a-f0-9]{64}\.png", path.name) and f"previews/{path.name}" not in referenced:
            path.unlink()
    save()
    print(f"Wrote {len(entries)} catalog entries to {catalog_path}", flush=True)


def markdown_cell(value: object) -> str:
    text = html.escape(str(value), quote=False)
    return text.replace("|", "&#124;").replace("\r\n", "<br>").replace("\n", "<br>")


def package_status(package: PackageReport) -> str:
    return "error" if package.errors else ("warning" if package.warnings else "ok")


def package_report_name(number: int, package: PackageReport) -> str:
    stem = re.sub(r"[^A-Za-z0-9._-]+", "_", package.path.stem).strip("._")
    return f"{number:04d}_{stem or 'package'}.md"


def build_index(source: Path, packages: list[PackageReport]) -> str:
    valid_packages = sum(not package.errors for package in packages)
    total_entries = sum(len(package.entries) for package in packages)
    duplicate_entries = sum(
        entry.duplicate for package in packages for entry in package.entries
    )
    duplicate_packages = sum(
        any(entry.duplicate for entry in package.entries) for package in packages
    )
    error_count = sum(len(package.errors) for package in packages)
    warning_count = sum(len(package.warnings) for package in packages)

    lines = [
        "# MRP Package Report",
        "",
        f"- Source: `{markdown_cell(source)}`",
        f"- Packages: {len(packages)}",
        f"- Structurally valid packages: {valid_packages}",
        f"- Parsed resource entries: {total_entries}",
        f"- Packages with duplicate names: {duplicate_packages}",
        f"- Duplicate resource entries: {duplicate_entries}",
        f"- Errors: {error_count}",
        f"- Warnings: {warning_count}",
        "",
        "## Topics",
        "",
        "- [Metadata](metadata.md): package header fields",
        "- [Issues](issues.md): parse errors and warnings",
        "- [Resources](resources/): one resource index per package",
        "",
        "## Packages",
        "",
        "| # | Package | Entries | Status | Resource index |",
        "|---:|---|---:|---:|---|",
    ]
    for number, package in enumerate(packages, 1):
        report_name = package_report_name(number, package)
        lines.append(
            f"| {number} | {markdown_cell(package.path.name)} | {len(package.entries)} "
            f"| {package_status(package)} | [open](resources/{report_name}) |"
        )
    return "\n".join(lines)


def build_metadata(packages: list[PackageReport]) -> str:
    lines = [
        "# MRP Metadata",
        "",
        "| # | Package | Preview | Internal filename | Application | APPID | Version | Vendor | Description | Physical bytes | Declared bytes |",
        "|---:|---|---|---|---|---:|---:|---|---|---:|---:|",
    ]
    for number, package in enumerate(packages, 1):
        metadata = package.metadata
        preview = (
            f'<img src="{html.escape(package.preview, quote=True)}" alt="Preview" width="96">'
            if package.preview
            else "not captured"
        )
        lines.append(
            f"| {number} | {markdown_cell(package.path.name)} "
            f"| {preview} "
            f"| {markdown_cell(metadata.get('Internal filename', ''))} "
            f"| {markdown_cell(metadata.get('Application name', ''))} "
            f"| {markdown_cell(metadata.get('APPID (big-endian fields)', ''))} "
            f"| {markdown_cell(metadata.get('Version (big-endian fields)', ''))} "
            f"| {markdown_cell(metadata.get('Vendor', ''))} "
            f"| {markdown_cell(metadata.get('Description', ''))} "
            f"| {package.file_size} "
            f"| {markdown_cell(metadata.get('Declared file size', ''))} |"
        )
    return "\n".join(lines)


def build_issues(packages: list[PackageReport]) -> str:
    lines = ["# MRP Issues", ""]
    issue_count = sum(len(package.errors) + len(package.warnings) for package in packages)
    if not issue_count:
        lines.append("No parse errors or warnings.")
        return "\n".join(lines)

    for number, package in enumerate(packages, 1):
        if not package.errors and not package.warnings:
            continue
        report_name = package_report_name(number, package)
        lines.extend([f"## {markdown_cell(package.path.name)}", ""])
        lines.append(f"Resource index: [open](resources/{report_name})")
        for label, messages in (("Errors", package.errors), ("Warnings", package.warnings)):
            if messages:
                lines.append(f"\n**{label}:**")
                lines.extend(f"- {markdown_cell(message)}" for message in messages)
        lines.append("")
    return "\n".join(lines)


def build_resource_report(package: PackageReport) -> str:
    lines = [
        f"# Resources: {markdown_cell(package.path.name)}",
        "",
        f"Physical size: {package.file_size} bytes; "
        f"parsed entries: {len(package.entries)}.",
        "",
        "| # | Resource | Offset | Length | End | Extra | Duplicate |",
        "|---:|---|---:|---:|---:|---:|:---:|",
    ]
    for entry in package.entries:
        lines.append(
            f"| {entry.index} | {markdown_cell(entry.name)} | {entry.offset} "
            f"| {entry.length} | {entry.offset + entry.length} | {entry.reserved} "
            f"| {'yes' if entry.duplicate else ''} |"
        )
    if not package.entries:
        lines.append("| | No entries parsed | | | | | |")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Parse MRP package metadata and resource indexes into Markdown."
    )
    parser.add_argument("input", type=Path, help="An .mrp file or a directory of .mrp files")
    parser.add_argument("--catalog", action="store_true", help="Write a public JSON/PNG catalog instead of Markdown reports")
    parser.add_argument("--download-base-url", help="External package directory URL; GitHub tree URLs become blob links")
    parser.add_argument("--workers", type=int, default=4, help="Concurrent isolated catalog captures (1-16)")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Report directory (default: mrp-report beside the input directory)",
    )
    parser.add_argument(
        "--capture",
        action="store_true",
        help="Launch each package in the simulator and save a preview image",
    )
    parser.add_argument(
        "--resume-capture",
        action="store_true",
        help="With preview capture, keep valid existing previews and capture packages without them",
    )
    parser.add_argument(
        "--recapture-duplicate-previews",
        action="store_true",
        help="With --capture, recapture packages whose current previews are identical",
    )
    parser.add_argument(
        "--skip-sound-prompt",
        action="store_true",
        help="Detect sound-confirmation screens, click No, and recapture",
    )
    parser.add_argument(
        "--refresh-splash-screens",
        action="store_true",
        help="Detect shared logo splash screens, wait for the next screen, then skip any sound prompt",
    )
    parser.add_argument(
        "--duplicate-preview-min",
        type=int,
        default=7,
        help="Minimum identical previews to treat as a shared splash/loading screen (default: 7)",
    )
    parser.add_argument(
        "--simulator",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "bin" / ("main.exe" if os.name == "nt" else "main"),
        help="Simulator executable (default: bin/main.exe on Windows, bin/main on Linux)",
    )
    parser.add_argument(
        "--runtime-dir",
        type=Path,
        help="Runtime asset directory (default: VMRP_RUNTIME_DIR or assets/runtime)",
    )
    parser.add_argument("--capture-delay", type=int, default=1200, help="Delay before capture, in ms")
    parser.add_argument("--capture-timeout", type=int, default=15, help="Per-package timeout, in seconds")
    parser.add_argument("--sound-prompt-threshold", type=float, default=0.8, help="Prompt image match threshold (default: 0.8)")
    parser.add_argument("--sound-prompt-wait", type=int, default=5000, help="Wait before clicking No, in ms")
    parser.add_argument("--post-skip-delay", type=int, default=5000, help="Wait after pressing right softkey before capture, in ms")
    parser.add_argument("--splash-threshold", type=float, default=0.9, help="Logo splash image match threshold (default: 0.9)")
    parser.add_argument("--splash-wait", type=int, default=7000, help="Wait for splash screen to advance, in ms")
    args = parser.parse_args()
    if args.catalog and not args.download_base_url:
        parser.error("--catalog requires --download-base-url")
    if not 1 <= args.workers <= 16:
        parser.error("--workers must be between 1 and 16")

    source = args.input.expanduser().resolve()
    if source.is_file():
        package_paths = [source]
        default_output = source.parent / f"{source.stem}-report"
    elif source.is_dir():
        package_paths = sorted(
            path.resolve()
            for path in source.rglob("*.mrp")
            if path.is_file()
        )
        default_output = source / "mrp-report"
    else:
        parser.error(f"Input path does not exist: {source}")

    output = args.output.expanduser().resolve() if args.output else default_output
    if output.suffix.lower() == ".md":
        parser.error("Output must be a directory, not a Markdown file path.")
    if output == source or output in source.parents:
        parser.error("Output directory cannot be the input path or one of its parents.")
    package_paths = [
        path
        for path in package_paths
        if path != output and output not in path.parents
    ]
    if not package_paths:
        parser.error(f"No .mrp files found under {source}")
    if args.capture_delay < 0 or args.capture_delay > 60000:
        parser.error("--capture-delay must be between 0 and 60000 ms")
    if args.capture_timeout < 1:
        parser.error("--capture-timeout must be at least 1 second")
    if not 0.0 <= args.sound_prompt_threshold <= 1.0:
        parser.error("--sound-prompt-threshold must be between 0 and 1")
    if not 0.0 <= args.splash_threshold <= 1.0:
        parser.error("--splash-threshold must be between 0 and 1")
    if args.sound_prompt_wait < 0 or args.sound_prompt_wait > 60000:
        parser.error("--sound-prompt-wait must be between 0 and 60000 ms")
    if args.post_skip_delay < 0 or args.post_skip_delay > 60000:
        parser.error("--post-skip-delay must be between 0 and 60000 ms")
    if args.splash_wait < 0 or args.splash_wait > 60000:
        parser.error("--splash-wait must be between 0 and 60000 ms")
    if args.duplicate_preview_min < 2:
        parser.error("--duplicate-preview-min must be at least 2")
    if args.recapture_duplicate_previews or args.skip_sound_prompt or args.refresh_splash_screens or args.resume_capture:
        args.capture = True
    if args.refresh_splash_screens:
        args.skip_sound_prompt = True

    reports = [parse_package(path) for path in package_paths]
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.mkdir(parents=True, exist_ok=True)
        if args.catalog:
            generate_catalog(source, reports, output, args)
            return 0
        clear_report_output(output, preserve_previews=args.resume_capture)
        captured = 0
        if args.capture:
            runtime_dir = (
                args.runtime_dir.expanduser().resolve()
                if args.runtime_dir
                else Path(os.environ.get("VMRP_RUNTIME_DIR", "assets/runtime")).expanduser().resolve()
            )
            simulator = args.simulator.expanduser().resolve()
            captured = capture_previews(
                reports,
                output,
                simulator,
                runtime_dir,
                args.capture_delay,
                args.capture_timeout,
                args.duplicate_preview_min if args.recapture_duplicate_previews else 0,
                args.skip_sound_prompt,
                args.sound_prompt_threshold,
                args.sound_prompt_wait,
                args.post_skip_delay,
                args.refresh_splash_screens,
                args.splash_threshold,
                args.splash_wait,
                args.resume_capture,
            )
        resources_dir = output / "resources"
        resources_dir.mkdir(exist_ok=True)
        (output / "index.md").write_text(
            build_index(source, reports), encoding="utf-8", newline="\n"
        )
        (output / "metadata.md").write_text(
            build_metadata(reports), encoding="utf-8", newline="\n"
        )
        (output / "issues.md").write_text(
            build_issues(reports), encoding="utf-8", newline="\n"
        )
        for number, report in enumerate(reports, 1):
            report_path = resources_dir / package_report_name(number, report)
            report_path.write_text(
                build_resource_report(report), encoding="utf-8", newline="\n"
            )
    except (OSError, ValueError) as exc:
        print(f"Could not write report {output}: {exc}", file=sys.stderr)
        return 1

    valid = sum(not report.errors for report in reports)
    entries = sum(len(report.entries) for report in reports)
    print(
        f"Wrote reports to {output}\n"
        f"Packages: {len(reports)} ({valid} structurally valid), resources: {entries}\n"
        f"Files: index.md, metadata.md, issues.md, resources/{len(reports)} package reports\n"
        f"Previews: {captured} saved"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())