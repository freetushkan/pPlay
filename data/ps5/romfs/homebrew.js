async function main() {
    const PAYLOAD = window.workingDir + '/pplay.elf';
    const ARGS = []
    const ENVVARS = {LD_LIBRARY_PATH: window.workingDir};

    return {
        mainText: "pPlay",
        secondaryText: 'pPlay Media Player',
        onclick: async () => {
            return {
                path: PAYLOAD,
                args: ARGS,
                env: ENVVARS
            };
        }
    };
}
