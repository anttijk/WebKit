import { useState } from "react";
import { createPortal } from "react-dom";

import Layout from "@/partials/layout/layout";
import Section from "../section/section";
import Toast from "@/components/toast/toast";

import { useDataContext } from "@/context/data-context";

export default function Page({ id }) {
    const { content } = useDataContext();
    const [showPortal, setShowPortal] = useState(Boolean(content[id].notification));

    function closePortal() {
        setShowPortal(false);
    }

    function onAccept() {
        closePortal();
    }

    function onReject() {
        closePortal();
    }

    return (
        <>
            <Layout id={id}>
                {content[id].sections.map((section) =>
                    <Section key={section.id} section={section} />
                )}
            </Layout>
            {showPortal && content[id].notification ? createPortal(<Toast notification={content[id].notification} onAccept={onAccept} onReject={onReject} onClose={onReject} />, document.getElementById("notifications-container")) : null}
        </>
    );
}
