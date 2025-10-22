# Copyright 1999-2025 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Id$

EAPI=7

inherit cmake git-r3

DESCRIPTION="SPICE client with output in the Kitty Graphics protocol"
HOMEPAGE="https://github.com/nemuTUI/spice-kitten"
EGIT_REPO_URI="https://github.com/nemuTUI/spice-kitten"
SRC_URI=""

LICENSE="BSD-2"
SLOT="0"

RDEPEND="
	dev-libs/openssl
	media-libs/libpng
	x11-libs/libX11"
DEPEND="${RDEPEND}
	app-emulation/spice-protocol"
